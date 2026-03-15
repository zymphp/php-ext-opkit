<?php
namespace OpKit\Composer;

use Symfony\Component\Console\Input\InputInterface;
use Symfony\Component\Console\Output\OutputInterface;

class CleanCommand extends BaseOpKitCommand
{
    protected function configure()
    {
        $this->setName('opkit-clean')
             ->setDescription('Clean OpKit extension build artifacts.');
    }

    public function execute(InputInterface $input, OutputInterface $output)
    {
        $io = $this->getIO();
        $io->write("<info>OpKit: Starting cleanup of build artifacts...</info>");

        $extensionDir = $this->getExtensionDir();
        if (!$extensionDir) {
            $io->writeError("<error>Could not find OpKit directory.</error>");
            return 1;
        }

        // 1. Run make clean if Makefile exists
        if (file_exists($extensionDir . DIRECTORY_SEPARATOR . 'Makefile')) {
            $io->write("Step 1: Running make clean...");
            try {
                $this->runProcess("make clean", $extensionDir, $io);
            } catch (\Exception $e) {
                $io->writeError("<warning>make clean failed: " . $e->getMessage() . "</warning>");
                $io->writeError("<info>Continuing anyway...</info>");
            }
        } else {
            $io->write("Skipping make clean (Makefile not found).");
        }

        // 2. Run phpize --clean
        $phpize = $this->getPhpize();
        $io->write("Step 2: Running $phpize --clean...");
        try {
            $this->runProcess($phpize . " --clean", $extensionDir, $io);
        } catch (\Exception $e) {
            $io->writeError("<error>$phpize --clean failed: " . $e->getMessage() . "</error>");
            return 1;
        }

        $io->write("\n<info>OpKit build artifacts cleaned successfully!</info>");

        return 0;
    }
}
