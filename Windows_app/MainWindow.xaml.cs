using System.Windows;
using System.Windows.Media;
using BLETester.Controllers;

namespace BLETester
{
    public partial class MainWindow : Window
    {
        private BLEController _bleController;

        public MainWindow()
        {
            InitializeComponent();
            _bleController = new BLEController();
            DataContext = _bleController;

            // Subscribe to property changes to update UI
            _bleController.PropertyChanged += BLEController_PropertyChanged;
        }

        private void BLEController_PropertyChanged(object sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(BLEController.IsConnected))
            {
                UpdateConnectionIndicator();
            }
            else if (e.PropertyName == nameof(BLEController.IsTransferActive))
            {
                UpdateStartStopButton();
            }
        }

        private void UpdateConnectionIndicator()
        {
            StatusIndicator.Fill = _bleController.IsConnected ? Brushes.Green : Brushes.Red;
        }

        private void UpdateStartStopButton()
        {
            StartStopButton.Content = _bleController.IsTransferActive ? "Stop Transfer" : "Start Transfer";
        }

        private async void ScanButton_Click(object sender, RoutedEventArgs e)
        {
            ScanButton.IsEnabled = false;
            await _bleController.ScanForDeviceAsync();
            ScanButton.IsEnabled = !_bleController.IsConnected;
        }

        private async void StartStopButton_Click(object sender, RoutedEventArgs e)
        {
            if (_bleController.IsTransferActive)
            {
                await _bleController.StopTransferAsync();
            }
            else
            {
                await _bleController.StartTransferAsync();
            }
        }
    }
}
