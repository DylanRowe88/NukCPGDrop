using Xunit;
using Bunit;
using Microsoft.AspNetCore.Components;
using NukCPGDrop.Ui.Components;
using NukCPGDrop.Ui.Models;

namespace NukCPGDrop.Ui.Tests.Components;

public class DifficultySelectorTests : TestContext
{
    [Fact]
    public void DifficultySelector_HighlightsActive()
    {
        var cut = RenderComponent<DifficultySelector>(p => p
            .Add(s => s.Selected, Difficulty.Short)
        );

        var buttons = cut.FindAll(".diff-btn");
        Assert.Equal(3, buttons.Count);
        Assert.Contains(buttons, b => b.ClassList.Contains("active"));
    }

    [Fact]
    public void DifficultySelector_FiresOnChanged()
    {
        Difficulty selected = Difficulty.Long;
        var cut = RenderComponent<DifficultySelector>(p => p
            .Add(s => s.Selected, Difficulty.Long)
            .Add(s => s.OnDifficultyChanged,
                 EventCallback.Factory.Create<Difficulty>(this, (d) => selected = d))
        );

        var buttons = cut.FindAll(".diff-btn");
        buttons[2].Click();
        Assert.Equal(Difficulty.Random, selected);
    }
}
