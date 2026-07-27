using System.Text.Json.Serialization;

namespace NukCPGDrop.Ui.Models;

public enum Difficulty
{
    Long = 0,
    Short = 1,
    Random = 2
}

public class DropConfiguration
{
    public Difficulty Difficulty { get; set; } = Difficulty.Short;
    public bool DoubleDrop { get; set; } = false;
    public int DropCount { get; set; } = 0;
    public int CustomInterval { get; set; } = 2000;
    public int RangeMin { get; set; } = 300;
    public int RangeMax { get; set; } = 2000;
}

public class LedColor
{
    public int R { get; set; }
    public int G { get; set; }
    public int B { get; set; }
}

public class WifiInfo
{
    public int Rssi { get; set; }
    public int Clients { get; set; }
    public string Version { get; set; } = "";
}

public class SystemStatus
{
    [JsonPropertyName("difficulty")]
    public int Difficulty { get; set; }

    [JsonPropertyName("double_drop")]
    public bool DoubleDrop { get; set; }

    [JsonPropertyName("drop_count")]
    public int DropCount { get; set; }

    [JsonPropertyName("held")]
    public bool[] Held { get; set; } = new bool[6];

    [JsonPropertyName("pca9685_present")]
    public bool Pca9685Present { get; set; }

    [JsonPropertyName("custom_interval")]
    public int CustomInterval { get; set; }

    [JsonPropertyName("range_min")]
    public int RangeMin { get; set; }

    [JsonPropertyName("range_max")]
    public int RangeMax { get; set; }

    [JsonPropertyName("led")]
    public LedColor? Led { get; set; }

    [JsonPropertyName("wifi")]
    public WifiInfo? Wifi { get; set; }
}

public class DropCommand
{
    public int? Id { get; set; }
}
