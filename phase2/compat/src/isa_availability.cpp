// The MinGW CRT does not publish MSVC's runtime ISA dispatch variable. Keep the
// generic x86-64/SSE2 path; it is correct on every x64 Windows target.
extern "C"
{
    int __isa_available = 0;
}
