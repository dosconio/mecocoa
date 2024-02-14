## Memory of developing Mecocoa



- [Q 20240207](https://stackoverflow.com/questions/22983797/is-it-possible-to-link-16-bit-code-and-32-bit-code): **It is possible for a ELF file to contain different BITS of a platform**.
- E 20240207: Based on past experience, flat mode may have an advantage in returning 16 bits because **the segments are not limited by length**(MIN\~MAX).



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







