<?php
namespace OpKit\Composer;

use Composer\Command\BaseCommand;
use Symfony\Component\Console\Input\InputInterface;
use Symfony\Component\Console\Output\OutputInterface;

abstract class BaseOpKitCommand extends BaseCommand
{
    public static function executeCommand($event)
    {
        $command = new static();
        $command->setComposer($event->getComposer());
        $command->setIO($event->getIO());

        $io = $event->getIO();
        $input = $io->isInteractive() ? new \Symfony\Component\Console\Input\ArgvInput([]) : new \Symfony\Component\Console\Input\ArrayInput([]);

        // ConsoleIO implements IOInterface, but execute needs OutputInterface.
        $output = new \Symfony\Component\Console\Output\ConsoleOutput();

        return $command->execute($input, $output);
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

    protected function runInteractiveProcess($command, $cwd, $io)
    {
        // Use STDIN/STDOUT/STDERR to allow password prompt (interactive)
        $process = proc_open($command, [0 => STDIN, 1 => STDOUT, 2 => STDERR], $pipes, $cwd);
        if (is_resource($process)) {
            $returnVar = proc_close($process);
            if ($returnVar !== 0) {
                throw new \Exception("Command '$command' failed with exit code $returnVar");
            }
        } else {
            throw new \Exception("Could not start process: $command");
        }
    }

    protected function getPhpize()
    {
        $phpBinDir = dirname(PHP_BINARY);
        $phpize = $phpBinDir . DIRECTORY_SEPARATOR . 'phpize';
        if (!is_executable($phpize)) {
            $phpize = 'phpize';
        }
        return $phpize;
    }

    protected function getPhpConfig()
    {
        $phpBinDir = dirname(PHP_BINARY);
        $phpConfig = $phpBinDir . DIRECTORY_SEPARATOR . 'php-config';
        if (!is_executable($phpConfig)) {
            $phpConfig = 'php-config';
        }
        return $phpConfig;
    }
}
