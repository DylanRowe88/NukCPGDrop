using System.Text.Json.Serialization;

namespace NukCPGDrop.Ui.Models;

public enum Difficulty
{
    Long = 0,
    Short = 1,
    Random = 2
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
    [JsonPropertyName("clients_list")]
    public ClientInfo[]? ClientsList { get; set; }
}

public class ClientInfo
{
    public string Mac { get; set; } = "";
    public int Rssi { get; set; }
}

public class SystemStatus
{
    [JsonPropertyName("difficulty")]
    public int Difficulty { get; set; }

    [JsonPropertyName("drop_count")]
    public int DropCount { get; set; }

    [JsonPropertyName("held")]
    public bool[] Held { get; set; } = new bool[16];

    [JsonPropertyName("pca9685_present")]
    public bool Pca9685Present { get; set; }

    [JsonPropertyName("custom_interval")]
    public int CustomInterval { get; set; }

    [JsonPropertyName("range_min")]
    public int RangeMin { get; set; }

    [JsonPropertyName("range_max")]
    public int RangeMax { get; set; }

    [JsonPropertyName("sv_start_pos")]
    public int SvStartPos { get; set; }

    [JsonPropertyName("sv_stop_pos")]
    public int SvStopPos { get; set; }

    [JsonPropertyName("led")]
    public LedColor? Led { get; set; }

    [JsonPropertyName("wifi")]
    public WifiInfo? Wifi { get; set; }

    [JsonPropertyName("sound_enabled")]
    public bool SoundEnabled { get; set; } = true;

    [JsonPropertyName("active_servos")]
    public int ActiveServos { get; set; } = 16;
}

public class DropConfiguration
{
    public Difficulty Difficulty { get; set; } = Difficulty.Short;
    public int DropCount { get; set; } = 0;
    public int CustomInterval { get; set; } = 2000;
    public int RangeMin { get; set; } = 300;
    public int RangeMax { get; set; } = 2000;
    public int SvStartPos { get; set; } = 0;
    public int SvStopPos { get; set; } = 180;
    public int ActiveServos { get; set; } = 16;
    public bool SoundEnabled { get; set; } = true;
}

public class DropCommand
{
    public int? Id { get; set; }
}
