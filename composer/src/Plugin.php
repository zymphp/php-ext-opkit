<?php
namespace OpKit\Composer;

use Composer\Composer;
use Composer\EventDispatcher\EventSubscriberInterface;
use Composer\IO\IOInterface;
use Composer\Plugin\PluginInterface;
use Composer\Plugin\Capable;
use Composer\Plugin\Capability\CommandProvider as CommandProviderCapability;
use Composer\Script\Event;
use Composer\Script\ScriptEvents;

class Plugin implements PluginInterface, EventSubscriberInterface, Capable
{
    /** @var Composer */
    protected $composer;

    /** @var IOInterface */
    protected $io;

    public function activate(Composer $composer, IOInterface $io)
    {
        $this->composer = $composer;
        $this->io = $io;
    }

    public function deactivate(Composer $composer, IOInterface $io)
    {
    }

    public function uninstall(Composer $composer, IOInterface $io)
    {
    }

    public function getCapabilities()
    {
        return [
            CommandProviderCapability::class => CommandProvider::class,
        ];
    }

    public static function getSubscribedEvents()
    {
        return [
            ScriptEvents::POST_INSTALL_CMD => 'onPostInstallUpdate',
            ScriptEvents::POST_UPDATE_CMD => 'onPostInstallUpdate',
            ScriptEvents::POST_AUTOLOAD_DUMP => 'onPostAutoloadDump',
        ];
    }

    public function onPostInstallUpdate(Event $event)
    {
        $this->compile($event);
    }

    public function onPostAutoloadDump(Event $event)
    {
        $this->compile($event);
    }

    protected function compile(Event $event)
    {
        if (!extension_loaded('opkit')) {
            $this->io->writeError("<warning>OpKit extension not loaded. Skipping auto-compilation.</warning>");
            return;
        }

        $configPath = getcwd() . DIRECTORY_SEPARATOR . 'opkit.json';
        if (!file_exists($configPath)) {
            // Not a project using OpKit configuration
            return;
        }

        $phpc = $this->findPhpc();
        if (!$phpc) {
            $this->io->writeError("<error>Could not find 'phpc' executable. Skipping auto-compilation.</error>");
            return;
        }

        $this->io->write("<info>OpKit: Running auto-compilation...</info>");
        $cmd = escapeshellarg(PHP_BINARY) . " " . escapeshellarg($phpc);

        // Pass the config file explicitly to ensure it uses the project root's config
        $cmd .= " -c " . escapeshellarg($configPath);

        passthru($cmd, $returnVar);

        if ($returnVar === 0) {
            $this->io->write("<info>OpKit: Auto-compilation successful.</info>");
        } else {
            $this->io->writeError("<error>OpKit: Auto-compilation failed with exit code $returnVar.</error>");
        }
    }

    protected function findPhpc()
    {
        // 1. Try vendor/bin/phpc
        $vendorBinDir = $this->composer->getConfig()->get('bin-dir');
        $phpcInVendor = $vendorBinDir . DIRECTORY_SEPARATOR . 'phpc';
        if (file_exists($phpcInVendor)) {
            return $phpcInVendor;
        }

        // 2. Try current package's bin if we are in development
        $phpcInPackage = dirname(__DIR__, 2) . DIRECTORY_SEPARATOR . 'bin' . DIRECTORY_SEPARATOR . 'phpc';
        if (file_exists($phpcInPackage)) {
            return $phpcInPackage;
        }

        // 3. Try global PATH
        $path = getenv('PATH');
        $paths = explode(PATH_SEPARATOR, $path);
        foreach ($paths as $p) {
            $full = $p . DIRECTORY_SEPARATOR . 'phpc';
            if (file_exists($full) && is_executable($full)) {
                return $full;
            }
        }

        return null;
    }
}
