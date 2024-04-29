// ASCII C TAB4 LF
// Attribute: 
// LastCheck: 20240215
// AllAuthor: @dosconio
// ModuTitle: Shell Real-16

extern void Dbg_Func(void);
extern void ReturnModeProt32(void);

static unsigned char logo[] = {"DOSCON.IO"};

void main()
{
    Dbg_Func();
    ReturnModeProt32();
    return;
}
