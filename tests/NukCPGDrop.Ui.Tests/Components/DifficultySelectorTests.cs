using Xunit;
using Bunit;
using Microsoft.AspNetCore.Components;
using NukCPGDrop.Ui.Components;

namespace NukCPGDrop.Ui.Tests.Components;

public class DifficultySelectorTests : TestContext
{
    [Fact]
    public void DifficultySelector_RendersRangeSlider()
    {
        var cut = RenderComponent<DifficultySelector>(p => p
            .Add(s => s.Interval, 1000)
        );

        var slider = cut.Find(".rs-track");
        Assert.NotNull(slider);
    }

    [Fact]
    public void DifficultySelector_RendersRandomToggle()
    {
        var cut = RenderComponent<DifficultySelector>(p => p
            .Add(s => s.RandomEnabled, true)
        );

        var checkbox = cut.Find("input[type='checkbox']");
        Assert.NotNull(checkbox);
    }
}
