/***
	bitlash-eeprom.c

	Bitlash is a tiny language interpreter that provides a serial port shell environment
	for bit banging and hardware hacking.

	See the file README for documentation.

	Bitlash lives at: http://bitlash.net
	The author can be reached at: bill@bitlash.net

	Copyright (C) 2008-2012 Bill Roy

	Permission is hereby granted, free of charge, to any person
	obtaining a copy of this software and associated documentation
	files (the "Software"), to deal in the Software without
	restriction, including without limitation the rights to use,
	copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the
	Software is furnished to do so, subject to the following
	conditions:

	The above copyright notice and this permission notice shall be
	included in all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
	OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
	NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
	HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
	WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
	OTHER DEALINGS IN THE SOFTWARE.

02/2018 fdufnews added EXTENDED_FILE_MANAGER to add some functons for extended "file " management
 		cmd_peep peep waits for 2 arguments (starting and ending address)
        added 'file management', ls, ll, cat
               ls list only function name
               ll list function name, size of function, total size of functions and holes in EEPROM
               cat funcname print a function
               cd to change active script source either EEPROM or RAM
               pwd to print current active script source
               cp to copy a script from current "drive" to the other one


***/
#include "bitlash.h"


/***
	This is a tiny attribute-value pair database for small EEPROM devices.
***/


// scan from addr for an occupied byte
int findoccupied(int addr) {
	while (addr < ENDDB) {
		if (eeread(addr) != EMPTY) return addr;
		addr++;
	}
	return FAIL;
}


// return the address of the first unused space at or after addr
int findunoccupied(int addr) {
	while (addr < ENDDB) {
		if (eeread(addr) == EMPTY) return addr;
		addr++;
	}
	return FAIL;
}


// find the end of this occupied string slot.  returns location or ENDDB.
int findend(int addr) {
	while (++addr < ENDDB) {
		byte c = eeread(addr);
		if (c == EMPTY) return addr;	// return pointer to first empty byte
		if (!c) return (++addr);		// or first byte past terminator
	}
	return ENDDB;
}


// return true if string in EEPROM at addr matches string at str
char eestrmatch(int addr, char *str) {
	while (*str) if (eeread(addr++) != *str++) return 0;
	if (eeread(addr) == 0) return 1;	// ended at the same place?
	return 0;
}


// find an entry in the db; return offset of id or FAIL
int findKey(char *id) {
int start = STARTDB;
	while (start < ENDDB-4) {
		// find the next entry
		start = findoccupied(start);
		if (start == FAIL) return FAIL;

		// start points to EEPROM id - check for match with id
		if (eestrmatch(start, id)) return start;

		// no match - skip the id and its value and continue scanning
		start = findend(start);		// scan past id
		start = findend(start);		// and value
	}
	return FAIL;
}


// Look up an entry by key.  Returns -1 on fail else addr of value.
int getValue(char *key) {
	int kaddr = findKey(key);
	return (kaddr < 0) ? kaddr : findend(kaddr);
}


// find an empty space of a given size or eep
int findhole(int size) {
int starthole = STARTDB, endhole;
	for (;;) {
		if (starthole + size > ENDDB) break;		// ain't gonna fit
		starthole = findunoccupied(starthole);		// first byte of next hole, or
		if (starthole == FAIL) break;				// outa holes

		endhole = findoccupied(starthole);			// first byte or next block, or
		if (endhole == FAIL) endhole = ENDDB+1;		// the first byte thou shall not touch

		// endhole is now on first char of next non-empty block, or one past ENDDB
		if ((endhole - starthole) >= size) return starthole;	// success
		starthole = endhole;		// find another hole
	}
	overflow(M_eeprom);
	return 0;		// placate compiler
}




///////////////////////////////
//
// Writing to the EEPROM
//

// Save string at str to EEPROM at addr
void saveString(int addr, char *str) {
	while (*str) eewrite(addr++, *str++);
	eewrite(addr, 0);
}

// erase string at addy.  return addy of byte past end.
int erasestr(int addr) {
	for (;;) {
		byte c = eeread(addr);
		if (c == EMPTY) return addr;
		eewrite(addr++, EMPTY);
		if (!c) return addr;
	}
}

// erase entry by id
void eraseentry(char *id) {
	int entry = findKey(id);
	if (entry >= 0) erasestr(erasestr(entry));
}

// parsestring helpers
void countByte(char c) { expval++; }
void saveByte(char c) { eewrite(expval++, c); }



// Parse and store a function definition
//
void cmd_function(void) {
char id[IDLEN+1];			// buffer for id

	getsym();				// eat "function", get putative id
	if ((sym != s_undef) && (sym != s_script_eeprom) &&
		(sym != s_script_progmem) && (sym != s_script_file)) unexpected(M_id);
	strncpy(id, idbuf, IDLEN+1);	// save id string through value parse
	eraseentry(id);

	getsym();		// eat the id, move on to '{'

	if (sym != s_lcurly) expected(s_lcurly);

	// measure the macro text using skipstatement
	// fetchptr is on the character after '{'
	//
	// BUG: This is broken for file scripts
	char *startmark = (char *) fetchptr;		// mark first char of macro text
	void skipstatement(void);
	skipstatement();				// gobble it up without executing it
	char *endmark = (char *) fetchptr;		// and note the char past '}'

	// endmark is past the closing '}' - back up and find it
	do {
		--endmark;
	} while ((endmark > startmark) && (*endmark != '}'));

	int idlen = strlen(id);
	int addr = findhole(idlen + (endmark-startmark) + 2);	// longjmps on fail
	if (addr >= 0) {
		saveString(addr, id);		// write the id and its terminator
		addr += idlen + 1;		// advance to payload offset
		while (startmark < endmark) eewrite(addr++, *startmark++);
		eewrite(addr, 0);
	}

	msgpl(M_saved);
}


// print eeprom string at addr
void eeputs(int addr) {
	for (;;) {
		byte c = eeread(addr++);
		if (!c || (c == EMPTY)) return;
#if 0
		//else if (c == '"') { spb('\\'); spb('"'); }
		else if (c == '\\') { spb('\\'); spb('\\'); }
		else if (c == '\n') { spb('\\'); spb('n'); }
		else if (c == '\t') { spb('\\'); spb('t'); }
		else if (c == '\r') { spb('\\'); spb('r'); }
		else if ((c >= 0x80) || (c < ' ')) {
			spb('\\'); spb('x');
			if (c < 0x10) spb('0'); printHex(c);
		}
#endif
		else spb(c);
	}
}


/* File management
 * Functions that dispaly the content of the attribute-value pairs data base
 * depending on the processor the number of function is different
 * for small processors :
 * 	ls: displays avpb content
 * 	peep: use no argument
 */
#if defined(EXTENDED_FILE_MANAGER)
#warning compiling extended file management
/*
 * for larger processors :
 * 	ls: displays the name of the functions
 * 	ll: displays the name and size of the functions and the number and size of holes in the EEPROM
 * 	cat: displays the code of a function
 * 	peep use 2 arguments (starting and ending address)
 * 	cd to change active source of script (either of EEPROM or RAM)
 * 	pwd to print the name of the current active source of script
 * 	cp to copy a script from current "drive" to the other one
*/


// cmd_ll
// list the strings in the avpdb
// long listing with name and size for each program
// displays holes and their size
void cmd_ll(void) {
int start = STARTDB;
int end;
int holesize=0;
int filesize=0;
int nbFiles=0;
int nbHoles=0;
int size=0;
char buffer[16];
char *ptr;
int count=0;

	for (;;) {
		byte c = eeread(start);
		if (c==EMPTY){				// We've found a hole in the database
			end = findoccupied(start); // find next file
			if (end == FAIL){
				sp("hole           ");
				size = ENDEEPROM - start;	// size of the last hole
				sp(itoa(size,buffer,10));	// print it
				speol();
				holesize+=size;			// accumulate hole size
				nbHoles++;
				speol();
				// print number of holes and their size
				itoa(nbHoles,buffer,10);
				ptr = buffer;
				size = 0;
				while(*(ptr++)) size++;
				size = 6-size;
				while(size--) spb(' ');
				sp(buffer);	// print it
				sp(" hole(s) for a size of ");
				sp(itoa(holesize,buffer,10));	// print it
				sp(" bytes");
				speol();
				// print number of files and their size
				itoa(nbFiles,buffer,10);
				ptr = buffer;
				size = 0;
				while(*(ptr++)) size++;
				size = 6-size;
				while(size--) spb(' ');
				sp(buffer);	// print it
				sp(" file(s) for a size of ");
				sp(itoa(filesize,buffer,10));	// print it
				sp(" bytes");
				speol();
				return;
			}
			sp("hole           ");
			size = end - start;		// size of the hole we've found
			sp(itoa(size,buffer,10));			// print it
			nbHoles++;
			holesize+=size;			// accumulate hole size
			start = end;			// new start
		} else {					// We've found the beginning of a file
			eeputs(start);			// print the name of the function
			end = findend(start);	// end of name
			int nb = 16-(end-start);
			while(nb--) spb(' '); 	// fill with space upto 16 places
			end = findend(end);	// end of function
			size = end - start;		// size of the file we've found
			sp(itoa(size,buffer,10));	// print it
			nbFiles++;
			filesize+=size;			// accumulate file size
			start = end;			// new start
		}
		if (count & 1)
			speol();
		else {
			if (size>999){
				sp(   "     |     ");
			} else if (size>99){
				sp(  "      |     ");
			} else if (size>9){
				sp( "       |     ");
			} else {
				sp("        |     ");
			}
		}
		count++;
	}
}

// cmd_ls
// list the name of the functions in the avpdb
// short listing with only the names
void cmd_ls(void) {
int start = STARTDB;
int end;
int count=0;
	for (;;) {
		// find the next entry
		start = findoccupied(start);
		if (start == FAIL) { speol(); return;}

		count++;
		eeputs(start);				// print name
		end = findend(start);		// end of name
		if ((count & 3) !=0){
			int nb = 16-(end-start);
			while(nb--) spb(' '); // fill with space upto 16 places
		} else {
			speol();
		}
		start = findend(end);
	}
}

// cmd_cat
// display the code of a function
void cmd_cat(char *id){
	int entry = findKey(id);
	if (entry == FAIL){
		unexpected(M_arg);
	} else {
		msgp(M_function);
		spb(' ');
		eeputs(entry);
		spb(' ');
		spb('{');
		entry = findend(entry);
		eeputs(entry);
		spb('}');
		spb(';');
		speol();
	}
}

// cmd_pwd
// get current working directory
void cmd_pwd(void){
	sp(disk==0?"EEPROM":"RAM");
	speol();
}

// cmd_cd
// change active drive (script source)
// source can be EEPROM or RAM
// we only test the first character so user can enter
// cd eeprom or cd e
// cd ram or cd r
// we shall stop all task running in background before changing the "drive"
void cmd_cd(char *id){
	initTaskList();		// stop the tasks running in the background
	if (id[0] == 'r'){
		disk = DISK_RAM;
	}else if (id[0] == 'e'){
		disk = DISK_EEPROM;
	}else{
		unexpected(M_arg);
	}
	sp("Drive is ");
	cmd_pwd();
}

//
// getString
// arguments
//		start starting point in drive source (EEPROM or virtual EEPROM)
//		buf pointer to buffer in which string will be copied
//		maxsize size of copy buffer
void getString(int start, char *buf, int maxsize){
	int size=0;
	uint8_t c;
	char *ptr = buf;
	do{
		c = eeread(start + size++);
		*ptr++ = c;
	}while((size < maxsize) && c!=0);
	*ptr=0;		// tag end of string
}

//
// copy
// argument is index of script ID in source (either RAM or EEPROM)
// source is on the active drive, destination is on the other drive
int copy(int addrs){
	char id[IDLEN+1];			// buffer for id
	uint8_t buf[STRVALSIZE];
	int source = addrs; // index of source
	int end = 0;		// end of source
	int size = 0;		// size of source
	int dest = 0;		// index for destination

	end = findend(source);	// we'll get end of ID
	end = findend(end);		// that's end of script
	size = end - source;	// size of the script to copy

	getString(source,id, IDLEN);	// copy id string from source to id buffer
	disk = disk ^ 1;		// disk is either 1 or 0 so switch from source to destination
	eraseentry(id);			// in case ID allready exists in destination erase it
	dest = findhole(size);	// get the index of a hole large enough to copy or FAIL
	if (dest == FAIL){
		overflow(M_eeprom);	// no more room on the drive
	}
	disk = disk ^ 1;		// switch to the source drive
	for(int index= 0; index < size; index++){
		buf[index] = eeread(source + index); // fill buffer with the script
	}
	disk = disk ^ 1;		// switch to the destination drive
	for(int index= 0; index < size; index++){
		eewrite(dest + index, buf[index]); // copy buffer in the destination drive
	}
	disk = disk ^ 1;		// switch to the source drive
	return(source + size);	// return address pointing to the end of the source
}

//
// cmd_cp
// copy a script or all the scripts from one "drive" to the other
// the source is the current drive the destination is the other one
// cp scriptname or cp *
// we shall stop all task running in background because the "drives" will switch during copy
void cmd_cp(void){
	int entry;
	boolean all=false;
	char *id = (char*)++fetchptr;		// get current position in input buffer
	if (id[0]=='*'){			// if copy all scripts
		entry = findoccupied(0) ;	// search the first script starting from index 0 of drive
		all = true;
	}
	else {
		entry = findKey(id);	// get address of script
	}
	if (entry == FAIL) unexpected(M_arg); // unknown script or no script
	initTaskList();				// stop running tasks before switching drives
	do{
		sp("Copying "); eeputs(entry); speol();
		entry = copy(entry);
		if (all){
			entry = findoccupied(entry);	// get address of next script or FAIL
		}
	}while(all && entry!=FAIL);
	while(*(char*)fetchptr++);	// skip argument
	*(char*)fetchptr--=0;
}

//
// cmd_peep
// 02/2018 fdufnews added 2 parameters starting and ending address
// this will help managing processors with large EEPROM where the dump will exceed the size of the display
//
void cmd_peep(void) {
int i=0;
int end=0;

	i=getnum() & 0xFFFFFFE0;
	if (i<0 || i>ENDEEPROM) i=0;
	end=getnum();
	if (end<i || end>ENDEEPROM) end=ENDEEPROM;
	while (i <= end) {
//		if (!(i&63)) {speol(); printHex(i+0xe000); spb(':'); }
		if (!(i&31)) {speol(); printHex(i+0xe000); spb(':'); }
		if (!(i&7)) spb(' ');
		if (!(i&3)) spb(' ');
		byte c = eeread(i) & 0xff;

		//if (c == 0) spb('\\');
		if (c == 0) spb('$');
		//else if ((c == 255) || (c < 0)) spb('.');
		else if (c == 255) spb('.');
		else if (c < ' ') spb('^');
		else spb(c);
		i++;
	}
	speol();
}

#else
// -----------standard file manager ----------------------
//
// list the strings in the avpdb
void cmd_ls(void) {
int start = STARTDB;
	for (;;) {
		// find the next entry
		start = findoccupied(start);
		if (start == FAIL) return;

		msgp(M_function);
		spb(' ');
		eeputs(start);
		spb(' ');
		spb('{');
		start = findend(start);
		eeputs(start);
		spb('}');
		spb(';');
		speol();
		start = findend(start);
	}
}

void cmd_peep(void) {
int i=0;
	while (i <= ENDEEPROM) {
		if (!(i&63)) {speol(); printHex(i+0xe000); spb(':'); }
		if (!(i&7)) spb(' ');
		if (!(i&3)) spb(' ');
		byte c = eeread(i) & 0xff;

		//if (c == 0) spb('\\');
		if (c == 0) spb('$');
		//else if ((c == 255) || (c < 0)) spb('.');
		else if (c == 255) spb('.');
		else if (c < ' ') spb('^');
		else spb(c);
		i++;
	}
	speol();
}
#endif


