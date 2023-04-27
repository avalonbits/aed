	.assume ADL = 1
	.org $A0000

	jp _start

_debug01:	.ds 3,0
_debug02:	.ds 3,0

	.align 64

	.db "MOS"
	.db 00h
	.db 01h

; Program entry point.
_start:
	; Initialize screen and text buffer.
	call _s_init
	call tb_init

@keyloop:
	call _process_input

	; If returns non-zero, we can exit the editor.
	or a
	jr Z, @keyloop

@done:
	call _s_done
	ld hl, 0
	ret


; PROCESS_INPUT
; Returns:
; - A: Status code
;   * 0: Success
;   * 1: Exit program
_process_input:
	; Get sysvars
	ld a, 08h
	rst.lil 08h

	; Call getkey
	ld a, 00h
	rst.lil 08h

	; First, check if we have the CTRL key pressed.
	bit 0, (ix+06h)

	; If the bit isn't set, then we can continue processing as normal.
	jp Z, @normal

	; We have CTRL pressed, get the scancode for the pressed key.
	ld a, (ix+17h)
	; CTRL+q or CTRL+Q exists the editor.
	cp 26h
	jp Z, @exit
	cp 40h
	jp Z, @exit

	; No more CTRL processing.
	ld a,0
	ret


@normal:
	; 7F and anything below 32 is a control char.
	cp 32
	jp M, @control
	cp 7Fh
	jp Z, @control

	; Printable character.
	; Add the character to the screen, replcaing the cursor.
	call _s_putc

	; Now add it to the text buffer.
	call tb_putc
	jr @done

@control:
	; Get keycode from sysvar. It is on offset 17h and is 1 byte.
	ld a, (ix+17h)

	; Left arrow.
	cp 9Ah
	jr Z, @m_left

	; Right arrow.
	cp 9Ch
	jr Z, @m_right

	; Delete.
	cp 82h
	jr Z, @m_del

	; Backspace.
	cp 84h
	jr Z, @m_bkspace

	; Home
	cp 85h
	jr Z, @m_home
	cp 86h
	jr Z, @m_home

	; Ignore other keys
	ld a,0
	ret

@m_left:
	; First replace the cursor with the character under it.
	call _replace_cur

	; Now move the cursor to the left in the buffer.
	call tb_left
	ld a, b
	or a
	jr NZ, @done

	; Move the cursor to left on VDP.
	ld a, 8
	rst.lil 10h

	; Dec curx
	ld a, (_s_curx)
	dec a
	ld (_s_curx), a
	jr @done

@m_right:
	call _replace_cur

	call tb_right
	ld a, b
	or a
	jr NZ, @done

	; Move the curosr right on VDP.
	ld a, 9
	rst.lil 10h

	; Inc curx
	ld a, (_s_curx)
	inc a
	ld (_s_curx), a
	jr @done

@m_del:
	call _replace_cur
	call tb_delete
	call _s_delete
	jr @done

@m_bkspace:
	call _s_bkspace
	call tb_bkspace
	jr @done

@m_home:
	call _replace_cur
	call tb_home
	xor a
	ld (_s_curx), a
	call _s_cback
	jr @done

@done:
	; The cursor is no longer on the screen. Bring it back.
	call tb_getc
	or a
	jr NZ, @cursor
	ld a, 32

@cursor:
	call _s_scur
	ld a, 0
	ret

@exit:
	ld a, 1
	ret

; REPLACE_CUR: Print the caracter under the cursor, replacing it on screen.
; Preserves:
; - AF.
_replace_cur:
	; Get the character currently under the cursor
	push af
	call tb_getc

	; If 0, there is no character, just use space.
	or a
	jr Z, @space

	; If its a carriage return, we are at end of line, so use space.
	cp '\r'
	jr Z, @space

	; Anyhing else, use it.
	jr @replace

@space:

	; We have no printable character under the cursor. Print space.
	ld a, 32

@replace:
	call _s_hcur

	; We are done.
	pop af
	ret



_enable_cursor:	.db 23,1,1
_scroll_up:	.db 23,7,0,3,8
_str:		.db "Hello, world!",0
_str_end:


;**** SCREEN BUFFER ****

; Cursor is relative to the number of chars.
; Highest supported resolution is 1280x720, which with an 8x8 font means
; 160x90 chars, fitting each dimension in a single byte.
_s_curx:	.db 0
_s_cury:	.db 0

; Max width and height in chars. This should be set on init and on every
; resolution change.
_s_maxw:	.db 0
_s_maxh:	.db 0
_s_eos:		.db 0

; Screen foreground and background colors.
_s_fg:		.db 15
_s_bg:		.db 0

; VDP init string:
; - Disables cursor.
; - Sets foreground and background colors.
; - Clears screen
; - Moves cursor home.
_init_vdp:	.db 23,1,0,17,15,17,128,12,30
_init_vdp_end:


; S_INIT: Initializes the screen for the editor.
; Preserves:
; - AF, BC, HL.
_s_init:
	push af
	push bc
	push hl

	; Init VDP for our text editor
	ld hl, _init_vdp
	ld bc, _init_vdp_end-_init_vdp
	rst.lil 18h

	; Get sysvars
	ld a, 08h
	rst.lil 08h

	; Store #cols.
	ld a, (ix+13h)
	dec a
	ld (_s_maxw), a

	; Store #rows.
	ld a, (ix+14h)
	dec a
	ld (_s_maxh), a

	; Init cursor.
	ld a, 0
	ld (_s_curx), a
	ld (_s_cury), a
	ld (_s_eos), a

	; Show the cursor on screen
	ld a, 32
	call _s_scur

	; Init is done.
	pop hl
	pop bc
	pop af
	ret


_vdp_tabxy:	.db 31,0,0

; _S_CBACK: Moves cursor to _s_curx,_s_cury
; Preserves:
; - AF, BC, HL
_s_cback:
	push af
	push bc
	push hl
	push ix

	ld ix, _vdp_tabxy
	ld a, (_s_curx)
	ld (ix+1), a
	ld a, (_s_cury)
	ld (ix+2), a
	ld hl, _vdp_tabxy
	ld bc, 3
	rst.lil 18h

	pop ix
	pop hl
	pop bc
	pop af
	ret

; _S_MCUR: Moves cursor to B,C
_s_mcur:
	push af
	push hl
	push ix

	ld ix, _vdp_tabxy
	ld (ix+1), b
	ld (ix+2), c
	ld hl, _vdp_tabxy
	ld bc, 3
	rst.lil 18h

	pop ix
	pop hl
	pop af
	ret

; S_HCUR: Hides the cursor
; - A: Character to use as cursor.
; Preserves
; - AF, BC, HL.
_s_hcur:
	push hl
	push bc
	push af

	; First set the correct colors.
	ld hl, _s_fg
	ld c, (hl)
	ld hl, _s_bg
	ld b, (hl)
	call _set_color

	; Now print the cursor
	rst.lil 10h

	; Because printing the cursor moves it on vdp, we need to bring it
	; back.
	call _s_cback

	; We are done
	pop af
	pop bc
	pop hl
	ret

; S_SCUR: Shows the cursor on screen.
; - A: Character to use as cursor.
; Preserves
; - AF, BC, HL.
_s_scur:
	push hl
	push bc
	push af

	; First reverse fg/bg.
	ld hl, _s_bg
	ld c, (hl)
	ld hl, _s_fg
	ld b, (hl)
	call _set_color

	; Now print the cursor.
	rst.lil 10h

	
	; First reverse fg/bg.
	; Because printing the cursor moves it on vdp, we need to bring it
	; back.
	call _s_cback
@done:
	; Now set the correct colors back.
	ld hl, _s_fg
	ld c, (hl)
	ld hl, _s_bg
	ld b, (hl)
	call _set_color

	; We are done.
	pop af
	pop bc
	pop hl
	ret

; SET_COLOR: Sets the colors for the screen
; Args:
; - C: Forground color.
; - B: Backgrond color.
; Preserves:
; - AF.
_set_color:
	push af
	; Set foreground color.
	ld a, 17
	rst.lil 10h
	ld a, c
	rst.lil 10h

	; Set background color.
	ld a, 17
	rst.lil 10h
	ld a, b
	add a, 128
	rst.lil 10h

	; We are done.
	pop af
	ret


; S_PUTC: Prints a character on screen.
; - A: Character to print.
; Preserves:
; - AF, HL.
_s_putc:
	push af
	push hl

	; Adjust the view if needed.
	call _s_pview
@print:

	; Print the char on screen
	rst.lil 10h


	; Inc curx and eos.
	ld a, (_s_curx)
	inc a
	ld (_s_curx), a
	
	; Increment eos if we haven't filled the line.
	ld a, (_s_maxw)
	ld b, a
	ld a, (_s_eos)
	cp b

	; If they are the same, we can' t increment.
	jr Z, @done

	; Still have space on the line.
	inc a
	ld (_s_eos), a

	; Signal we moved
	ld b, 0

@done:
	pop hl
	pop af
	ret

_s_offset:	.db 0,0,0

; S_VIEW: Adjust the charactor view so that we can scroll the line.
_s_pview:
	push af
	push de
	push bc
	push hl

	; Given that this is only called right before putting a character on
	; the screen, we want to make sure there is enough space for it.
	; There are cases to handle:
	; - Cursor at EOL:
   	;   * Shift everyone to the left to create a space. No tail work.
        ; - Cursor somewhere in line:
	;   * Move everyone after the cursor to the right, removing char if 
	;     needed.

	; So first, let's figure out where we are.
	ld a, (_s_curx)
	ld b, a
	ld a, (_s_maxw)
	cp b

	; If s_curx != s_maxw, then cursor is not at end of screen.
	jr NZ, @inline

	; We are at end of screen.
	; To shift we want to shift the line to the left. For that we define
	; The current line as the region we want to shift, shift it and then
	; redefine the region again.
	ld de, (_s_offset)
	inc de
	ld (_s_offset), de

	; With this, HL points to start of string, BC has the string count.
	call tb_gethead


@shift_left:
	inc hl
	dec de

	; If we still have chars to offset, loop back.
	ld a, d
	or e
	jr NZ, @shift_left


@print_left:
	; So now, we move cursor to home
	ld b, 0
	ld a, (_s_cury)
	ld c,a
	call _s_mcur

	; Print the string.
	ld bc, 0
	ld a, (_s_curx)
	dec a
	ld c, a
	rst.lil 18h

	; Move cursor one position back
	ld a, (_s_curx)
	dec a
	ld (_s_curx), a

	; Move cursor to curx,cury
	call _s_cback

	jr @done


@inline:
	; We have enough space. Then just adjust the tail.
	call tb_gettail
	ld a, b
	or c

	; If we have no tail, we are done.
	jr Z, @done

	; We have a tail. Move 1 space to the right,
	ld a, 9
	rst.lil 10h

	; Get the current position.
	ld a, (_s_curx)
	ld e, a

	; Print chars from tail until we are done or if we hit the end of the
	; screen.
@tail_print:
	; Did we hit end of screen?
	ld a, (_s_maxw)
	cp e
	jr Z, @move_back

	; Did we finish writing chars?
	ld a, b
	or c
	jr Z, @move_back

	; Update counters.
	inc e
	dec bc

	; Print char.
	ld a, (hl)
	inc hl
	push de
	push bc
	rst.lil 10h
	pop bc
	pop de

	; Next char.
	jr @tail_print


@move_back:
	; Move the cursor back to _s_curx,_s_cury
	call _s_cback

	
@done:
	pop hl
	pop bc
	pop de
	pop af
	ret

; S_BACKSPACE: Erase a char to the left of the cursor on screen.
; Preserves:
; - AF
_s_bkspace:
	push af

	; First check if we can move.
	ld a, (_s_curx)
	or a
	jr Z, @done

	; Remove cursor from screen
	call _replace_cur
	
	; Move cursor to the left.
	dec a
	ld (_s_curx), a
	call _s_cback

	; Get the tail
	call tb_gettail
	ld a, b
	or c
	jr Z, @space

	; We have tail, print it.
	rst.lil 18h

@space:
	; Print the space to remove the char from the screen.
	ld a, 32
	rst.lil 10h

	; Bring the cursor back
	call _s_cback

@done:
	pop af
	ret


; S_DELETE: Erase a char to the right of the cursor.
; Preserve:
; - AF
_s_delete:
	push af

	; Get the tail. If there are no chars, nothing to do.
	call tb_gettail
	ld a, b
	or c
	jr Z, @done

	; We have chars and we have previously call tb_delete. So, we just
	; need to print the tail + space char.
	push bc
	rst.lil 18h
	ld a, 32
	rst.lil 10h

	; Now bring back the cursor to the correct position.
	; Increment bc to account for the extra space print
	pop bc
	inc bc
@loop:
	call _s_cback
	dec bc
	ld a, b
	or c
	jr NZ, @loop

@done:
	pop af
	ret

;S_DONE: Restores the computer back to a usable state.
_s_done:
	; Clear screen.
	ld a, 12
	rst.lil 10h

	; Bring cursor home.
	ld a, 30
	rst.lil 10h

	; Renable cursor
	ld hl, _enable_cursor
	ld bc, 3
	rst.lil 18h

	; Done
	ret



;**** TEXT BUFFER ****

; Gap Buffer for the text.
_tb_start:	.ds 3,0	; Pointer to buf start.
_tb_end:	.ds 3,0	; Pointer to buf end.
_ccur:		.ds 3,0	; Pointer to cursor position in buf.
_cend:		.ds 3,0	; Pointer to buffer suffix.
_slptr:		.ds 3,0 ; Pointer to start of current line.
_elptr:		.ds 3,0 ; Pointer to end of current line.
_szptr:		.ds 3,0	; Pointer to line size.

; Line/Char position in buffer.
_lpos:		.ds 3,0
_cpos:		.ds 3,0

; Line count, init to 1.
_lcount:	.db 1,0,0
; Char count, init t0 0.
_ccount:	.ds 3,0

; TB_INIT: Initializes the text buffer.
; Preserves:
; - IX, HL, BC.
tb_init:
	push hl
	push bc

	; Set the start pointers for text buffer.
	ld hl, 40000h
	ld (_tb_start), hl
	ld (_szptr), hl

	; Every line starts with 3 bytes that is the size counter.
	ld hl, 40003h
	ld (_ccur), hl
	ld (_slptr), hl

	; Set the end pointers for text buffer.
	ld hl, A0000h
	ld (_tb_end), hl
	ld (_cend), hl
	ld (_elptr), hl

	; We are done.
	pop bc
	pop hl
	ret


; TB_GETC: Returns the current char under the cursor.
; Returns:
; - A: The ASCII code of current char, 32 (space) if buffer is empty.
; Preserves:
; - DE, HL.
tb_getc:
	; Preserve registers.
	push de
	push hl

	; If there are chars at end, then the cursor is covering a char.
	ld hl, (_elptr)
	ld de, (_cend)
	ld a,32

	scf
	ccf
	sbc hl,de
	jp Z, @done

	; Cursor is covering a char. Return it.
	ld de, (_cend)
	ld a, (de)

@done:
	pop hl
	pop de
	ret


; TB_LINE_NO: Returns the current line number.
; Returns:
; - BC: Line number.
tb_line_no:
	ld bc, (_lpos)
	ret



; TB_CHAR_NO: Returns the current char number at current line.
; Returns:
; - BC: Char number.
tb_char_no:
	ld bc, (_cpos)
	ret

; TB_LINE_COUNT: Returns the number of lines in the buffer.
tb_line_count:
	ld bc, (_lcount)
	ret


; TB_PUTC: Stores a char in the text buffer.
; Args:
; - A: Char to be stored.
; Preserves:
; - DE, HL
tb_putc:
	push de
	push hl

	; First check we can store
	ld hl, (_cend)
	ld de, (_ccur)
	scf
	ccf
	sbc hl,de

	; We can't store if there is no more space. For now, just return.
	jr Z, @done

	
	; First we add the char to the text buffer.
	ld de, (_ccur)
	ld (de), a
	inc de
	ld (_ccur), de

	; Now update the number of chars for the current line.
	ld hl, (_szptr)
	inc hl
	ld (_szptr), hl

	; Update the character position.
	ld hl, (_cpos)
	inc hl
	ld (_cpos), hl

	; Finally, update the character count.
	ld hl, (_ccount)
	inc hl
	ld (_ccount), hl

@done:
	; Now we are done.
	pop hl
	pop de
	ret


; TB_GETTAIL: Returns all characters at the end of the line.
; Returns:
; - HL: Pointer to start of tail
; - BC: Number of chars in tail til EOL.
; Preserves:
; - DE
tb_gettail:
	push de

	; We want to count how many chars we have between _cend and _elptr.
	ld bc, 0
	ld hl, (_elptr)
	ld de, (_cend)

	; Get the number of characters.
	scf
	ccf
	sbc hl, de

	; In order to get the full 24 bit value, we need to store the data
	; in memory.
	push hl
	ld hl, 0
	add hl, sp
	ld bc, (hl)
	pop hl

	; We now return _cend and count of chars in bc.
	ld hl, (_cend)
	pop de
	ret


; TB_GETHEAD: Returns all characters from start of line to current cursor.
; Returns:
; - HL: Pointer to start of head.
; - BC: Number of chars in tail til cursor.
; Preserves:
; - DE
tb_gethead:
	push de

	; Count how many chars we have between start of line and current
	; cursor position
	ld bc, 0
	ld hl, (_ccur)
	ld de, (_slptr)

	scf
	ccf
	sbc hl, de

	; Because its a 24 bit value, we need to store it in memory to copy it
	; to BC.
	push hl
	ld hl, 0
	add hl, sp
	ld bc, (hl)
	pop hl

	; We now return _slptr and count of chars in bc.
	ld hl, (_slptr)
	pop de
	ret


; TB_LEFT: Move the cursor left.
; Returns:
; - A: The character that the cursor was pointing to before moving.
; - B: 0 if move was successful.

tb_left:
	; Preserve registers
	push hl
	push de

	; Get cursor and check if we can move left.
	ld hl, (_ccur)
	ld de, (_slptr)
	scf
	sbc hl, de
	jp P, @move

	; Can't move.
	ld b, 1
	jr @done

@move:
	; Move _ccur and _cend pointers to the left.
	ld hl, (_ccur)
	ld de, (_cend)
	dec de
	dec hl

	; Move char in _ccur to _cend
	ld a, (hl)
	ld (de), a

	; Store updated pointers.
	ld (_cend), de
	ld (_ccur), hl

	; Decrement charactor position
	ld hl, (_cpos)
	dec hl
	ld (_cpos), hl

	; Signal we moved.
	ld b, 0

@done:
	; Restore registers.
	pop de
	pop hl
	ret


; TB_HOME: Move the cursor to the start of line.
tb_home:
	push af
	push bc

@loop:
	call tb_left
	ld a, b
	or a
	jr Z, @loop

@done:
	pop af
	pop bc
	ret


; TB_DELETE: Remove a character to the right of the cursor.
tb_delete:
	; Preserve registers
	push hl
	push de

	; Delete is equivalent to advancing the _cend pointer.
	ld hl, (_elptr)
	ld de, (_cend)
	scf
	ccf
	sbc hl, de
	jr Z, @done

	; We have chars to delete. Do it.
	inc de
	ld (_cend), de

@done:
	pop de
	pop hl
	ret

; TB_BKSPACE: Move the cursor left, delete the character to the left.
tb_bkspace:
	; Preserve registers
	push hl
	push de

	; Get cursor and check if we can move left.
	ld hl, (_ccur)
	ld de, (_slptr)
	scf
	sbc hl, de
	jp P, @move

	; Can't move.
	ld b, 1
	jr @done

@move:
	; Move _ccur left, effectively erasing the character.
	ld hl, (_ccur)
	dec hl

	; Store the updated pointer
	ld (_ccur), hl

	; Decrement character position
	ld hl, (_cpos)
	dec hl
	ld (_cpos), hl

	; Signal we moved
	ld b, 0

@done:
	; Restore registers
	pop de
	pop hl
	ret


; TB_RIGHT: Move the cursor right.
; Returns:
; - A: The character that the cursor is now pointing after moving.
; - B: 0 if the move was successful.
tb_right:
	; Preserve registers
	push hl
	push de

	; Get cursor and check if we can move right.
	ld hl, (_elptr)
	ld de, (_cend)
	scf
	sbc hl, de
	jp P, @move

	; Can't move.
	ld b, 1
	jr @done

@move:
	; Copy char in _cend to _ccur
	ld hl, (_cend)
	ld de, (_ccur)
	ld a, (hl)
	ld (de), a

	; Move pointers to the right
	inc de
	inc hl

	; Store updated pointers
	ld (_ccur), de
	ld (_cend), hl

	; Increment character position
	ld hl, (_cpos)
	inc hl
	ld (_cpos), hl

	; Signal we were able to move.
	ld b, 0

@done:
	; Restore registers.
	pop de
	pop hl
	ret


; TB_NEWLINE: Add a newline to the text buffer.
tb_newline:
	; First we add CRLF to the current line.
	ld a, '\r'
	call tb_putc
	ld a, '\n'
	call tb_putc

	; TODO(icc): things to be done still.


