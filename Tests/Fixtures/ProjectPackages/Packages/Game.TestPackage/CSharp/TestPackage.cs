using System.Runtime.InteropServices;

namespace Game.TestPackage;

public static class TestPackage
{
    [DllImport("Pkg.Game.TestPackage.Native")]
    public static extern int IndexTestPackage_Value();
}
