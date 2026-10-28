using System;
using System.Buffers;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Funke;
public enum FilterOption { Digits, Letters, Alphanumeric };
public enum CaseConvention { PascalCase, CamelCase, SnakeCase, KebabCase };

public static class TextUtils
{
    public const string Digits = "0123456789";
    public const string LettersUppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    public const string LettersLowercase = "abcdefghijklmnopqrstuvwxyz";
    public const string Letters = LettersLowercase + LettersUppercase;

    public const char Space = ' ';

    public static string RemoveChars(string input, params char[] nonAllowedChars) => FilterChars(input, nonAllowedChars, false);
    public static string KeepOnly(string input, params char[] allowedChars) => FilterChars(input, allowedChars, true);

    private static string FilterChars(string input, char[] chars, bool keepMatches)
    {
        ArgumentNullException.ThrowIfNull(input);
        ArgumentNullException.ThrowIfNull(chars);
        if (chars.Length == 0)
            return keepMatches ? string.Empty : input;

        ReadOnlySpan<char> source = input.AsSpan();
        int firstRemoved = keepMatches ? source.IndexOfAnyExcept(chars) : source.IndexOfAny(chars);
        if (firstRemoved < 0)
            return input;

        char[]? rented = null;
        Span<char> buffer = input.Length <= 256 ? stackalloc char[input.Length] : (rented = ArrayPool<char>.Shared.Rent(input.Length));
        try
        {
            source[..firstRemoved].CopyTo(buffer);
            int length = firstRemoved;
            for (int i = firstRemoved + 1; i < source.Length; i++)
            {
                if (chars.AsSpan().Contains(source[i]) == keepMatches)
                    buffer[length++] = source[i];
            }
            return new string(buffer[..length]);
        }
        finally
        {
            if (rented != null)
                ArrayPool<char>.Shared.Return(rented);
        }
    }

    public static char[] GetFilterCharset(FilterOption filterOption, bool useSpace = true)
    {
        string allowedChars = filterOption == FilterOption.Digits ? Digits : (filterOption == FilterOption.Letters ? Letters : (Digits + Letters));
        char[] result = new char[allowedChars.Length + (useSpace ? 1 : 0)];
        allowedChars.CopyTo(0, result, 0, allowedChars.Length);
        if (useSpace)
            result[^1] = Space;
        return result;
    }

    public static string ToHex(int value) => $"#{value:X2}";
    public static string ToHex(long value) => $"#{value:X2}";
    public static string ToHex(params int[] values)
    {
        var result = new StringBuilder("#");
        foreach (var value in values)
        {
            result.Append($"{value:X2}");
        }
        return result.ToString();
    }
    public static string ToHex(params long[] values)
    {
        var result = new StringBuilder();
        foreach (var value in values)
        {
            result.Append($"#{value:X2}");
        }
        return result.ToString();
    }


    // Counts space-separated segments, including empty ones, as string.Split(' ') does.
    public static int WordsCount(string s)
    {
        int count = 1;
        foreach (char c in s)
        {
            if (c == Space)
                count++;
        }
        return count;
    }

    public static string ToString(IEnumerable numerable)
    {
        var result = new StringBuilder("[");
        bool first = true;
        foreach (var item in numerable)
        {
            if (!first)
                result.Append(", ");
            result.Append(item.ToString());
            first = false;
        }
        return result.Append(']').ToString();
    }

    public static string ToCase(string s, CaseConvention convention)
    {
        ArgumentNullException.ThrowIfNull(s);
        if (convention < CaseConvention.PascalCase || convention > CaseConvention.KebabCase)
            return s;

        int length = 0;
        int wordCount = 0;
        foreach (Range range in s.AsSpan().Split(Space))
        {
            int wordLength = s.AsSpan(range).Length;
            if (wordLength == 0)
                continue;
            length += wordLength;
            wordCount++;
        }
        if (convention == CaseConvention.SnakeCase || convention == CaseConvention.KebabCase)
            length += System.Math.Max(0, wordCount - 1);

        return string.Create(length, (s, convention, culture: CultureInfo.CurrentCulture), static (destination, state) =>
        {
            char separator = state.convention == CaseConvention.SnakeCase ? '_' : '-';
            bool capitalize = state.convention == CaseConvention.PascalCase || state.convention == CaseConvention.CamelCase;
            int written = 0;
            foreach (Range range in state.s.AsSpan().Split(Space))
            {
                ReadOnlySpan<char> word = state.s.AsSpan(range);
                if (word.IsEmpty)
                    continue;
                if (!capitalize && written > 0)
                    destination[written++] = separator;

                Span<char> target = destination.Slice(written, word.Length);
                if (capitalize)
                {
                    target[0] = state.convention == CaseConvention.CamelCase && written == 0
                        ? char.ToLower(word[0], state.culture)
                        : char.ToUpper(word[0], state.culture);
                    word[1..].ToLower(target[1..], state.culture);
                }
                else
                {
                    word.ToLower(target, state.culture);
                }
                written += word.Length;
            }
        });
    }

    public static string WrapWith(string s, string beginMark, string endMark) => string.Concat(beginMark, s, endMark);
    public static string WrapWith(string s, string mark) => WrapWith(s, mark, mark);
    public static string WrapWith(string s, char beginMark, char endMark) => $"{beginMark}{s}{endMark}";
    public static string WrapWith(string s, char mark) => WrapWith(s, mark, mark);

    public static string ToColoredString(string text, string hexColor) 
    {
        return $"<color=#{hexColor}>{text}</color>";
    }

    public static int DifferentCharsCount(string s)
    {
        if (string.IsNullOrEmpty(s))
            return 0;

        return new HashSet<char>(s).Count;
    }

    public static string Capitalize(string input)
    {
        if (string.IsNullOrEmpty(input))
            return input;

        return string.Create(input.Length, (input, culture: CultureInfo.CurrentCulture), static (destination, state) =>
        {
            destination[0] = char.ToUpper(state.input[0], state.culture);
            state.input.AsSpan(1).ToLower(destination[1..], state.culture);
        });
    }
}
