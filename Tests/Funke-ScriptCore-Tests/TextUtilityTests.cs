using System;
using System.Collections;
using System.Globalization;
using Funke;
using Xunit;

namespace FunkeScriptCoreTests;

public class TextUtilityTests
{
    [Theory]
    [InlineData("a1-b2-a", "ab", "1-2-", "aba")]
    [InlineData("aabb", "aa", "bb", "aa")]
    [InlineData("abc", "", "abc", "")]
    [InlineData("", "abc", "", "")]
    [InlineData("\u00e4\u4e2d\u00e4", "\u00e4", "\u4e2d", "\u00e4\u00e4")]
    public void Filters_PreserveOrderAndRepeatedCharacters(string input, string characters, string removed, string kept)
    {
        Assert.Equal(removed, TextUtils.RemoveChars(input, characters.ToCharArray()));
        Assert.Equal(kept, TextUtils.KeepOnly(input, characters.ToCharArray()));
    }

    [Fact]
    public void Filters_ReturnOriginalStringWhenUnchanged()
    {
        string input = new string(new[] { 'a', 'b', 'a' });
        Assert.Same(input, TextUtils.RemoveChars(input, 'x'));
        Assert.Same(input, TextUtils.RemoveChars(input));
        Assert.Same(input, TextUtils.KeepOnly(input, 'a', 'b'));
    }

    [Theory]
    [InlineData(1)]
    [InlineData(256)]
    [InlineData(257)]
    public void Filters_HandleBoundaryRemovalsAndFullyRejectedInputs(int length)
    {
        string kept = new string('a', length - 1);
        Assert.Equal(kept, TextUtils.RemoveChars("#" + kept, '#'));
        Assert.Equal(kept, TextUtils.RemoveChars(kept + "#", '#'));
        Assert.Equal(kept, TextUtils.KeepOnly("#" + kept, 'a'));
        Assert.Equal(kept, TextUtils.KeepOnly(kept + "#", 'a'));
        Assert.Equal("", TextUtils.RemoveChars(new string('#', length), '#'));
        Assert.Equal("", TextUtils.KeepOnly(new string('#', length), 'a'));
    }

    [Theory]
    [InlineData(FilterOption.Digits, TextUtils.Digits)]
    [InlineData(FilterOption.Letters, TextUtils.Letters)]
    [InlineData(FilterOption.Alphanumeric, TextUtils.Digits + TextUtils.Letters)]
    public void GetFilterCharset_ReturnsExpectedCharactersAndIndependentArrays(FilterOption option, string expected)
    {
        Assert.Equal(expected, new string(TextUtils.GetFilterCharset(option, false)));
        char[] first = TextUtils.GetFilterCharset(option);
        Assert.Equal(expected + " ", new string(first));
        first[0] = '!';
        char[] second = TextUtils.GetFilterCharset(option);
        Assert.NotSame(first, second);
        Assert.Equal(expected + " ", new string(second));
    }

    [Fact]
    public void ToHex_PreservesEachOverloadsPrefixAndMinimumWidth()
    {
        Assert.Equal("#00", TextUtils.ToHex(0));
        Assert.Equal("#123", TextUtils.ToHex(0x123L));
        Assert.Equal("#FFFFFFFF", TextUtils.ToHex(-1));
        Assert.Equal("#FFFFFFFFFFFFFFFF", TextUtils.ToHex(-1L));
        Assert.Equal("#010AFF123FFFFFFFF", TextUtils.ToHex(new[] { 1, 10, 255, 0x123, -1 }));
        Assert.Equal("#01#0A#FF#123#FFFFFFFFFFFFFFFF", TextUtils.ToHex(new long[] { 1, 10, 255, 0x123, -1 }));
        Assert.Equal("#", TextUtils.ToHex(Array.Empty<int>()));
        Assert.Equal("", TextUtils.ToHex(Array.Empty<long>()));
    }

    [Theory]
    [InlineData("", 1)]
    [InlineData("word", 1)]
    [InlineData("a  b", 3)]
    [InlineData(" a ", 3)]
    [InlineData("   ", 4)]
    [InlineData("a\tb\nc", 1)]
    public void WordsCount_PreservesLiteralSpaceSeparatedEmptySegments(string input, int expected)
    {
        Assert.Equal(expected, TextUtils.WordsCount(input));
    }

    [Fact]
    public void ToString_FormatsSinglePassEnumerationWithoutTrailingSeparator()
    {
        Assert.Equal("[]", TextUtils.ToString(Array.Empty<object>()));
        Assert.Equal("[only]", TextUtils.ToString(new[] { "only" }));
        var item = new FormattedItem();
        var sequence = new SinglePassEnumerable(new object[] { 12, "text", item });
        Assert.Equal("[12, text, formatted]", TextUtils.ToString(sequence));
        Assert.Equal(1, item.FormatCount);
    }

    [Theory]
    [InlineData(CaseConvention.PascalCase, "HelloWorld")]
    [InlineData(CaseConvention.CamelCase, "helloWorld")]
    [InlineData(CaseConvention.SnakeCase, "hello_world")]
    [InlineData(CaseConvention.KebabCase, "hello-world")]
    public void ToCase_CombinesWordsAndIgnoresRepeatedSpaces(CaseConvention convention, string expected)
    {
        WithCulture("en-US", () => Assert.Equal(expected, TextUtils.ToCase("  hELLO   WORLD  ", convention)));
    }

    [Theory]
    [InlineData(CaseConvention.PascalCase)]
    [InlineData(CaseConvention.CamelCase)]
    [InlineData(CaseConvention.SnakeCase)]
    [InlineData(CaseConvention.KebabCase)]
    public void ToCase_HandlesEmptyAndAllSpaceInputs(CaseConvention convention)
    {
        Assert.Equal("", TextUtils.ToCase("", convention));
        Assert.Equal("", TextUtils.ToCase("   ", convention));
    }

    [Theory]
    [InlineData("en-US", "IiIi", "iiIi", "ii_ii", "ii-ii")]
    [InlineData("tr-TR", "\u0130\u0131I\u0131", "i\u0131I\u0131", "i\u0131_\u0131\u0131", "i\u0131-\u0131\u0131")]
    public void ToCase_UsesCurrentCulture(string culture, string pascal, string camel, string snake, string kebab)
    {
        WithCulture(culture, () =>
        {
            Assert.Equal(pascal, TextUtils.ToCase("iI II", CaseConvention.PascalCase));
            Assert.Equal(camel, TextUtils.ToCase("iI II", CaseConvention.CamelCase));
            Assert.Equal(snake, TextUtils.ToCase("iI II", CaseConvention.SnakeCase));
            Assert.Equal(kebab, TextUtils.ToCase("iI II", CaseConvention.KebabCase));
        });
    }

    [Theory]
    [InlineData(CaseConvention.PascalCase, "A\U00010428B\U00010428")]
    [InlineData(CaseConvention.CamelCase, "a\U00010428B\U00010428")]
    [InlineData(CaseConvention.SnakeCase, "a\U00010428_b\U00010428")]
    [InlineData(CaseConvention.KebabCase, "a\U00010428-b\U00010428")]
    public void ToCase_LowercasesSupplementaryCharactersInWordRemainders(CaseConvention convention, string expected)
    {
        WithCulture("en-US", () => Assert.Equal(expected, TextUtils.ToCase("a\U00010400 B\U00010400", convention)));
    }

    [Fact]
    public void ToCase_UnknownConventionReturnsOriginalString()
    {
        string input = new string(new[] { ' ', 'A', ' ', 'B', ' ' });
        Assert.Same(input, TextUtils.ToCase(input, (CaseConvention)100));
    }

    [Fact]
    public void WrapWith_IncludesBothMarkersInEveryOverload()
    {
        Assert.Equal("<b>text</b>", TextUtils.WrapWith("text", "<b>", "</b>"));
        Assert.Equal("**text**", TextUtils.WrapWith("text", "**"));
        Assert.Equal("[text]", TextUtils.WrapWith("text", '[', ']'));
        Assert.Equal("'text'", TextUtils.WrapWith("text", '\''));
        Assert.Equal("[]", TextUtils.WrapWith("", '[', ']'));
        Assert.Equal("text", TextUtils.WrapWith("text", ""));
    }

    [Theory]
    [InlineData("", 0)]
    [InlineData("aabb", 2)]
    [InlineData("aA", 2)]
    [InlineData("\U0001F600\U0001F601", 3)]
    public void DifferentCharsCount_CountsDistinctUtf16CodeUnits(string input, int expected)
    {
        Assert.Equal(expected, TextUtils.DifferentCharsCount(input));
    }

    [Theory]
    [InlineData("en-US", "istanBUL", "Istanbul")]
    [InlineData("tr-TR", "istanBUL", "\u0130stanbul")]
    [InlineData("tr-TR", "iI", "\u0130\u0131")]
    [InlineData("en-US", "a\U00010400", "A\U00010428")]
    [InlineData("en-US", "", "")]
    public void Capitalize_PreservesCultureAndSupplementaryCasing(string culture, string input, string expected)
    {
        WithCulture(culture, () => Assert.Equal(expected, TextUtils.Capitalize(input)));
    }

    [Fact]
    public void NullAwareHelpers_PreserveNullBehavior()
    {
        Assert.Equal(0, TextUtils.DifferentCharsCount(null!));
        Assert.Null(TextUtils.Capitalize(null!));
    }

    private static void WithCulture(string culture, Action assertion)
    {
        CultureInfo previous = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo(culture);
            assertion();
        }
        finally
        {
            CultureInfo.CurrentCulture = previous;
        }
    }

    private sealed class SinglePassEnumerable(object[] values) : IEnumerable
    {
        private bool _enumerated;

        public IEnumerator GetEnumerator()
        {
            if (_enumerated)
                throw new InvalidOperationException("The sequence can only be enumerated once.");
            _enumerated = true;
            return values.GetEnumerator();
        }
    }

    private sealed class FormattedItem
    {
        public int FormatCount { get; private set; }

        public override string ToString()
        {
            FormatCount++;
            return "formatted";
        }
    }
}
