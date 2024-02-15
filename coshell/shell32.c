// ASCII C TAB4 CRLF
// Attribute: 
// LastCheck: 20240215
// AllAuthor: @dosconio
// ModuTitle: Shell Real-32

int main(void)
{
    unsigned char* Video = (unsigned char*)0xB8000;
    Video[0] = 'M';
    Video[1] = 0x70;
    return 0;
}

//{TODO} to be COTLAB.