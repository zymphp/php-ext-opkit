<?php
namespace OpKit\Composer;

use Symfony\Component\Console\Input\InputInterface;
use Symfony\Component\Console\Output\OutputInterface;

class BuildCommand extends BaseOpKitCommand
{
    protected function configure()
    {
        $this->setName('opkit-build')
             ->setDescription('Build and install the OpKit PHP extension from source.');
    }

    public function execute(InputInterface $input, OutputInterface $output)
    {
        $io = $this->getIO();
        $io->write("<info>OpKit: Starting extension build process...</info>");
        $io->write("<info>PHP Version: " . PHP_VERSION . " (" . PHP_BINARY . ")</info>");

        $extensionDir = $this->getExtensionDir();
        if (!$extensionDir || !file_exists($extensionDir . DIRECTORY_SEPARATOR . 'config.m4')) {
            $io->writeError("<error>Could not find config.m4 in OpKit directory.</error>");
            if ($extensionDir) {
                $io->writeError("Checked: $extensionDir");
            }
            return 1;
        }

        $phpize = $this->getPhpize();
        $phpConfig = $this->getPhpConfig();

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
        $io->write("To install, run: <comment>composer opkit-install</comment>");
        $io->write("Then add <comment>zend_extension=opkit.so</comment> to your php.ini.");

        return 0;
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
