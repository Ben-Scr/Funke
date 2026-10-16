using System;

namespace Funke;
/// <summary>
/// Marks a field or property to be hidden from the Funke Editor inspector,
/// even if it would normally be visible (e.g. public fields).
/// </summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public class HideFromEditorAttribute : Attribute
{
}
