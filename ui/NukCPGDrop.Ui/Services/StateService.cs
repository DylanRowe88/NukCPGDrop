using NukCPGDrop.Ui.Models;

namespace NukCPGDrop.Ui.Services;

public class StateService
{
    public DropConfiguration Config { get; set; } = new();
    public SystemStatus? LastStatus { get; set; }

    public event Action? OnChange;

    public void NotifyStateChanged() => OnChange?.Invoke();
}
