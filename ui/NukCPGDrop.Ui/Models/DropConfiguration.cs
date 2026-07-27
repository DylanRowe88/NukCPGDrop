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

public class SystemStatus
{
    public int Difficulty { get; set; }
    public bool DoubleDrop { get; set; }
    public int DropCount { get; set; }
    public bool[] Held { get; set; } = new bool[6];
    public bool Pca9685Present { get; set; }
    public int CustomInterval { get; set; }
    public int RangeMin { get; set; }
    public int RangeMax { get; set; }
    public LedColor? Led { get; set; }
}

public class DropCommand
{
    public int? Id { get; set; }
}
