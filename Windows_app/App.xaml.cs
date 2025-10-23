using System.Windows;
using BLETester.Interop;

namespace BLETester
{
    public partial class App : Application
    {
        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);

            // Initialize PSoC driver library
            if (!NativeMethods.psoc_driver_init())
            {
                MessageBox.Show("Failed to initialize PSoC driver library", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                Shutdown();
            }
        }

        protected override void OnExit(ExitEventArgs e)
        {
            // Cleanup PSoC driver library
            NativeMethods.psoc_driver_cleanup();
            base.OnExit(e);
        }
    }
}
