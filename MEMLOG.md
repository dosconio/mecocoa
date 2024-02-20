## Memory of developing Mecocoa


- R 20240219 **Realize** that 0x00000000~0x7FFFFFFF can reflect physical 0x80000000~0xFFFFFFFF. For 64-bit systems. This design of MSB is still used. Paging is not used or equivalent for 16-bit systems.
- [Q 20240207](https://stackoverflow.com/questions/22983797/is-it-possible-to-link-16-bit-code-and-32-bit-code): **It is possible for a ELF file to contain different BITS of a platform**.
- E 20240207: Based on past **experience**, flat mode may have an advantage in returning 16 bits because **the segments are not limited by length**(MIN\~MAX).



### Structure

Before 20240206:

```mermaid
graph LR
	Boot-->Kernel-->Kernel32-->Shell32
	Kernel32--->A
	Kernel32--->B
```

After 20240212:

```mermaid
graph LR
	BOOT-->KERNEL-->Shell16
	Shell16--ret-->PowerOff
	Shell16--command-->Shell32
	Shell32--ret-->Shell16
	Shell32--command-->PowerOff
	PowerOff--reset-->BOOT
	
```

After ...







