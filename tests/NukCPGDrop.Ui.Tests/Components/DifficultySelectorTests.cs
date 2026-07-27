using Xunit;
using Bunit;
using Microsoft.AspNetCore.Components;
using NukCPGDrop.Ui.Components;

namespace NukCPGDrop.Ui.Tests.Components;

public class DifficultySelectorTests : TestContext
{
    [Fact]
    public void DifficultySelector_RendersIntervalSlider()
    {
        var cut = RenderComponent<DifficultySelector>(p => p
            .Add(s => s.Interval, 1000)
        );

        var slider = cut.Find(".interval-slider");
        Assert.NotNull(slider);
    }

    [Fact]
    public void DifficultySelector_RendersDoubleDropToggle()
    {
        var cut = RenderComponent<DifficultySelector>(p => p
            .Add(s => s.DoubleDrop, true)
        );

        var checkbox = cut.Find("input[type='checkbox']");
        Assert.NotNull(checkbox);
    }

    [Fact]
    public void DifficultySelector_TogglesRandomRange()
    {
        var cut = RenderComponent<DifficultySelector>(p => p
            .Add(s => s.RandomEnabled, true)
            .Add(s => s.RangeMin, 300)
            .Add(s => s.RangeMax, 2000)
        );

        var rangeLabels = cut.FindAll(".range-value");
        Assert.NotEmpty(rangeLabels);
    }
}
