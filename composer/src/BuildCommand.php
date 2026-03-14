<?php
namespace OpKit\Composer;

use Composer\Command\BaseCommand;
use Symfony\Component\Console\Input\InputInterface;
use Symfony\Component\Console\Output\OutputInterface;

class BuildCommand extends BaseCommand
{
    public static function executeCommand($event)
    {
        $command = new self();
        $command->setComposer($event->getComposer());
        $command->setIO($event->getIO());

        $io = $event->getIO();
        $input = $io->isInteractive() ? new \Symfony\Component\Console\Input\ArgvInput([]) : new \Symfony\Component\Console\Input\ArrayInput([]);

        // ConsoleIO implements IOInterface, but execute needs OutputInterface.
        // In Composer context, we can use the Symfony application to get the output.
        $output = new \Symfony\Component\Console\Output\ConsoleOutput();

        return $command->execute($input, $output);
    }

    protected function configure()
    {
        $this->setName('opkit-build')
             ->setDescription('Build and install the OpKit PHP extension from source.');
    }

    public function execute(InputInterface $input, OutputInterface $output)
    {
        $io = $this->getIO();
        $io->write("<info>OpKit: Starting extension build process...</info>");

        $extensionDir = $this->getExtensionDir();
        if (!$extensionDir || !file_exists($extensionDir . DIRECTORY_SEPARATOR . 'config.m4')) {
            $io->writeError("<error>Could not find config.m4 in OpKit directory.</error>");
            if ($extensionDir) {
                $io->writeError("Checked: $extensionDir");
            }
            return 1;
        }

        // Check for required tools
        $phpBinDir = dirname(PHP_BINARY);
        $phpize = $phpBinDir . DIRECTORY_SEPARATOR . 'phpize';
        $phpConfig = $phpBinDir . DIRECTORY_SEPARATOR . 'php-config';

        if (!is_executable($phpize)) {
            $phpize = 'phpize';
        }
        if (!is_executable($phpConfig)) {
            $phpConfig = 'php-config';
        }

        $tools = [$phpize, 'make'];
        foreach ($tools as $tool) {
            $checkCmd = (strpos($tool, DIRECTORY_SEPARATOR) !== false) ? "test -x " . escapeshellarg($tool) : "command -v $tool";
            exec($checkCmd, $dummy, $returnVar);
            if ($returnVar !== 0) {
                $io->writeError("<error>Tool '$tool' not found. Please install it before building.</error>");
                return 1;
            }
        }

        $io->write("Step 1: Running $phpize...");
        $this->runProcess($phpize, $extensionDir, $io);

        $configureCmd = "./configure --enable-opkit";
        $checkPhpConfigCmd = (strpos($phpConfig, DIRECTORY_SEPARATOR) !== false) ? "test -x " . escapeshellarg($phpConfig) : "command -v $phpConfig";
        exec($checkPhpConfigCmd, $dummy, $returnVar);
        if ($returnVar === 0) {
            $configureCmd .= " --with-php-config=" . escapeshellarg($phpConfig);
        }

        $io->write("Step 2: Running configure...");
        $io->write("<comment>Command: $configureCmd</comment>");
        $this->runProcess($configureCmd, $extensionDir, $io);

        $io->write("Step 3: Running make...");
        $this->runProcess("make -j" . $this->getNproc(), $extensionDir, $io);

        $io->write("\n<info>OpKit extension built successfully!</info>");
        $io->write("To install, run: <comment>sudo make install</comment> in $extensionDir");
        $io->write("Then add <comment>zend_extension=opkit.so</comment> to your php.ini.");

        return 0;
    }

    protected function getExtensionDir()
    {
        // 1. Try relative to this file (development mode)
        $dir = dirname(__DIR__, 2);
        if (file_exists($dir . DIRECTORY_SEPARATOR . 'config.m4')) {
            return realpath($dir);
        }

        // 2. Try to find the package directory from Composer's repository
        $composer = $this->getComposer();
        if ($composer) {
            $package = $composer->getRepositoryManager()
                ->getLocalRepository()
                ->findPackage('zymphp/opkit', '*');

            if ($package) {
                $installPath = $composer->getInstallationManager()
                    ->getInstallPath($package);
                if ($installPath && file_exists($installPath . DIRECTORY_SEPARATOR . 'config.m4')) {
                    return realpath($installPath);
                }
            }
        }

        return null;
    }

    protected function runProcess($command, $cwd, $io)
    {
        $descriptorspec = [
            0 => ["pipe", "r"],
            1 => ["pipe", "w"],
            2 => ["pipe", "w"]
        ];

        $process = proc_open($command, $descriptorspec, $pipes, $cwd);
        if (is_resource($process)) {
            while ($s = fgets($pipes[1])) {
                $io->write($s, false);
            }
            while ($s = fgets($pipes[2])) {
                $io->writeError("<error>$s</error>", false);
            }
            fclose($pipes[0]);
            fclose($pipes[1]);
            fclose($pipes[2]);
            $returnVar = proc_close($process);
            if ($returnVar !== 0) {
                throw new \Exception("Command '$command' failed with exit code $returnVar");
            }
        } else {
            throw new \Exception("Could not start process: $command");
        }
    }

    protected function getNproc()
    {
        if (is_callable('shell_exec') && stripos(PHP_OS, 'WIN') === false) {
            $nproc = shell_exec('nproc');
            if ($nproc > 0) return trim($nproc);
        }
        return 1;
    }
}
