using Xunit;
using Bunit;
using Microsoft.AspNetCore.Components;
using NukCPGDrop.Ui.Components;

namespace NukCPGDrop.Ui.Tests.Components;

public class DropButtonTests : TestContext
{
    [Fact]
    public void DropButton_RendersLabel()
    {
        var cut = RenderComponent<DropButton>(p => p
            .Add(b => b.Label, "DROP ALL")
            .Add(b => b.Subtitle, "Start sequence")
        );

        var button = cut.Find("button");
        Assert.Contains("DROP ALL", button.TextContent);
        Assert.Contains("drop-btn", button.GetAttribute("class"));
    }

    [Theory]
    [InlineData(true)]
    [InlineData(false)]
    public void DropButton_DisabledState(bool disabled)
    {
        var cut = RenderComponent<DropButton>(p => p
            .Add(b => b.Label, "DROP")
            .Add(b => b.Disabled, disabled)
        );

        var button = cut.Find("button");
        if (disabled)
            Assert.NotNull(button.GetAttribute("disabled"));
        else
            Assert.Null(button.GetAttribute("disabled"));
    }

    [Fact]
    public void DropButton_FiresOnClick()
    {
        var clicked = false;
        var cut = RenderComponent<DropButton>(p => p
            .Add(b => b.Label, "DROP")
            .Add(b => b.OnClick, EventCallback.Factory.Create(this, () => clicked = true))
        );

        cut.Find("button").Click();
        Assert.True(clicked);
    }

    [Fact]
    public void DropButton_RendersDisabledAttribute()
    {
        var cut = RenderComponent<DropButton>(p => p
            .Add(b => b.Label, "DROP")
            .Add(b => b.Disabled, true)
        );

        var button = cut.Find("button");
        Assert.NotNull(button.GetAttribute("disabled"));
    }
}
