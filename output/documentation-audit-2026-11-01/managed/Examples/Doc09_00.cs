namespace Documentation9 {
#line 221 "C:/Users/benso/source/repos/Pending/Index/Github-Docs/Documentation/Packages/Writing-Packages.md"
using System.Runtime.InteropServices;

internal static class Native {
    private const string Lib = "Pkg.MyGame.Loot.Native";

    [DllImport(Lib, EntryPoint = "MyGame_Loot_RollDrop",
               CallingConvention = CallingConvention.Cdecl)]
    public static extern int RollDrop(int tableId);
}
}
