#if NETFRAMEWORK
// Polyfill required so `init`-only setters compile under net472
// (the type only exists in net5.0+).
namespace System.Runtime.CompilerServices
{
    using System.ComponentModel;

    [EditorBrowsable(EditorBrowsableState.Never)]
    internal static class IsExternalInit { }
}
#endif
