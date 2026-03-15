<?php
namespace OpKit\Composer;

use Symfony\Component\Console\Input\InputInterface;
use Symfony\Component\Console\Output\OutputInterface;

class InstallCommand extends BaseOpKitCommand
{
    protected function configure()
    {
        $this->setName('opkit-install')
             ->setDescription('Install the compiled OpKit extension.');
    }

    public function execute(InputInterface $input, OutputInterface $output)
    {
        $io = $this->getIO();
        $io->write("<info>OpKit: Starting extension installation...</info>");

        $extensionDir = $this->getExtensionDir();
        if (!$extensionDir) {
            $io->writeError("<error>Could not find OpKit directory.</error>");
            return 1;
        }

        if (!file_exists($extensionDir . DIRECTORY_SEPARATOR . 'Makefile')) {
            $io->writeError("<error>Makefile not found. Please run 'composer opkit-build' first.</error>");
            return 1;
        }

        $io->write("Step 1: Running make install...");
        try {
            // Try standard install first
            $this->runInteractiveProcess("make install", $extensionDir, $io);
        } catch (\Exception $e) {
            $io->write("<warning>Standard 'make install' failed (likely permission denied).</warning>");

            // Check if sudo is available
            exec("command -v sudo", $dummy, $returnVar);
            if ($returnVar === 0) {
                $io->write("<info>Retrying with sudo... You may be prompted for your password.</info>");
                try {
                    $this->runInteractiveProcess("sudo make install", $extensionDir, $io);
                } catch (\Exception $sudoE) {
                    $io->writeError("<error>sudo make install failed: " . $sudoE->getMessage() . "</error>");
                    return 1;
                }
            } else {
                $io->writeError("<error>make install failed and 'sudo' is not available.</error>");
                return 1;
            }
        }

        $io->write("\n<info>OpKit extension installed successfully!</info>");
        $io->write("Please ensure <comment>zend_extension=opkit.so</comment> is added to your php.ini.");
        $io->write("You can verify with: <comment>php -m | grep opkit</comment>");

        return 0;
    }
}
