	.assume ADL = 1
	.org $50000

	jp _start

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
	; Get sysvars and call getkey.
	ld a, 08h
	rst.lil 08h
	call _getkey

	; First check if the CTRL key is pressed.
	bit 0, (ix+06h)
	jr NZ, @normal

	; We have CTRL pressed.
	cp 'q'
	jp Z, @exit

@normal:
	; Anything below 32 and 7F is a control char.
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

	; ESC quits the program
	cp 7Dh
	jr Z, @exit

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

	; Ignore other keys/
	jp @done

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
	jr @done

@m_del:
	call _replace_cur
	call tb_delete
	call _s_delete
	jr @done

@m_bkspace:
	call _replace_cur
	call _s_bkspace
	call tb_bkspace
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

_getkey:
	ld a, 00h
	rst.lil 08h
	ret


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

; Scroll command. This is a template that we use to set the correct region.
_dscroll:	.db 28,0,0,0,0
_dscroll_end:

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

	; Store #cols and #rows for the current resolution.
	ld a, (ix+13h)
	ld (_s_maxw), a
	ld a, (ix+14h)
	ld (_s_maxh), a

	; Init cursor.
	ld a, 0
	ld (_s_curx), a
	ld (_s_cury), a

	; Define the scrolling region. X1 and Y1 is already 0. We need to set
	; X2 and Y2. For X2, we want the full column size. For Y2, we want
	; the full row size minus 8 pixels.

	; First, X2.
	ld ix, _dscroll
	ld a, (_s_maxw)
	ld (ix+3h), a

	; Now Y2, with 1 row of characters removed.
	ld a, (_s_maxh)
	dec a
	ld (ix+4h), a

	; Now set the scroll region
	ld hl, _dscroll
	ld bc, _dscroll_end-_dscroll
	rst.lil 18h

	; Show the cursor on screen
	ld a, 32
	call _s_scur

	; Init is done.
	pop hl
	pop bc
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
	ld a, 8
	rst.lil 10h

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

	; Because printing the cursor moves it on vdp, we need to bring it
	; back.
	ld a, 8
	rst.lil 10h

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
; - AF, BC.
_s_putc:
	push af
	push hl

	; First, check if we can advance the cursor.
	push af
	ld hl, _s_curx
	ld a, (_s_maxw)
	dec a
	sub a,(hl)
	pop af
	jr NZ, @print

	; Can't add to screen. For now, do nothing.

@print:
	; Print the char on screen
	rst.lil 10h


	; First Get the tail of the line and print it.
	call tb_gettail
	ld a, b
	or c

	; If zero, there is no tail to print.
	jr Z, @inc


	; We have a tail, print it.
	push bc
	rst.lil 18h
	pop bc

	; Since that moves the cursor in VDP, we need to bring it back the
	; same number of times we printed it.
@loop:
	ld a, 8
	rst.lil 10h
	dec bc
	ld a, b
	or c
	jp NZ, @loop

@inc:
	; Inc X position
	ld a, (_s_curx)
	inc a
	ld (_s_curx), a

	; Signal we moved
	ld b, 0

@done:
	pop hl
	pop af
	ret

; S_BACKSPACE: Erase a char to the left of the cursor on screen.
; Preserves:
; - AF
_s_bkspace:
	push af

	; First move the cursor one to the left.
	ld a, 8
	rst.lil 10h


	; Get the tail
	call tb_gettail
	ld a, b
	or c
	jr Z, @notail

	; We have tail. Print the tail + 1 space to move the tail 1 char left.
	push bc
	rst.lil 18h
	ld a, 32
	rst.lil 10h

	; Bring the cursor back to the correct position.
	pop bc
	inc bc ; to account for the space.
@loop:
	ld a, 8
	rst.lil 10h
	dec bc
	ld a, b
	or c
	jr NZ, @loop

	; Now we are done.
	jr @done


@notail:
	; Since we have no tail, we can print a space over the char.
	ld a, 32
	rst.lil 10h
	; Bring the cursor back
	ld a, 8
	rst.lil 10h

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
	ld a, 8
	rst.lil 10h
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

	; Restore full window scrolling.
	; First, X1,Y1
	ld ix, _dscroll
	ld (ix+1h), 0
	ld (ix+2h), 0

	; Now, X2.
	ld a, (_s_maxw)
	ld (ix+3h), a

	; Finally Y2
	ld a, (_s_maxh)
	ld (ix+4h), a

	; Set the scrolling region
	ld hl, _dscroll
	ld bc, _dscroll_end-_dscroll
	rst.lil 18h

	; Renable cursor
	ld hl, _enable_cursor
	ld bc, 3
	rst.lil 18h

	; Done
	ret



;**** TEXT BUFFER ****

; Gap Buffer for the text. 241k
_tb_start:	.db 0,0,6
_tb_end:	.db 0,C4h,8
_ccur:		.db 0,0,6
_cend:		.db 0,C4h,8

; Total char count
_ccount:	.db 0,0,0
; Absolute position in char buffer.
_cpos:		.db 0,0,0

; Char-per-line gap buffer for text. 15k, enough for 5k lines.
_lb_start:	.db 0,C4h,8
_lb_end:	.db 0,0,9
_lcur:		.db 0,C4h,8
_lend:		.db 0,0,9
_lcount:	.db 0,0,0
_lpos:		.db 0,0,0

_debug01:	.db A0h,0,Ah
_debug02:	.db A3h,0,Ah

; TB_INIT: Initializes the text buffer.
; Preserves:
; - HL, BC.
tb_init:
	push hl
	push bc

	ld hl, (_debug01)
	ld (hl), 0
	ld hl, (_debug02)
	ld (hl), 0

	; Set the start pointers for text buffer.
	ld hl, 60000h
	ld (_tb_start), hl
	ld (_ccur), hl

	; Set the end pointers for text buffer.
	ld hl, 8C400h
	ld (_tb_end), hl
	ld (_cend), hl

	; Set the start pointers for line buffer.
	ld (_lb_start), hl
	ld (_lcur), hl

	; Se the end pointers for line buffer.
	ld hl, 90000h
	ld (_lb_end), hl
	ld (_lend), hl

	; We set char count and pos to 0 and line count to 1.
	ld hl, 0
	ld (_ccount), hl
	ld (_cpos), hl
	ld (_lpos), hl

	ld hl, 1
	ld (_lcount), hl

	; For the char-per-line buffer, we need to set the first value to 0.
	ld hl, (_lcur)
	ld bc, 0
	ld (hl), bc

	; We are done.
	pop bc
	pop hl
	ret

; TB_GETC: Returns the current char under the cursor.
; Returns:
; - A: The ASCII code of current char, 0 if buffer is empty.
; Preserves:
; - DE, HL.
tb_getc:
	; Preserve registers.
	push de
	push hl


	; If there are chars at end, then the cursor covering a char.
	ld hl, (_tb_end)
	ld de, (_cend)
	ld a,0

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


; TB_PUTC: Stores a char in the text buffer.
; Args:
; - A: Char to be stored.
; Preserves:
; - DE, HL
tb_putc:
	; First we add the char to the text buffer.
	push de
	push hl
	ld de, (_ccur)
	ld (de), a
	inc de
	ld (_ccur), de

	; Now we update the number of chars.
	ld de, (_ccount)
	inc de
	ld (_ccount), de


	; Now update the number of chars for the current line.
	ld hl, (_lcur)
	ld de, (hl)
	inc de
	ld (hl), de

	; Finally update the character position.
	ld hl, (_cpos)
	inc hl
	ld (_cpos), hl

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
	; We want to count how many chars we have between _cend and either
	; _tb_end or the first new line char.
	ld bc, 0
	ld hl, (_cend)
	ld de, (_tb_end)

@loop:
	push hl
	scf
	ccf
	sbc hl, de
	pop hl

	; If there are any, we are done.
	jr Z, @done

	; We have a char. If it's a new line, we are also done
	ld a, (hl)
	cp '\r'
	jr Z, @done

	; It's a regular char. Count it, go to the next char.
	inc bc
	inc hl
	jr @loop

@done:
	; We now return _cend and count of chars in bc.
	ld hl, (_cend)
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
	ld de, (_tb_start)
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

; TB_DELETE: Remove a character to the right of the cursor.
tb_delete:
	; Preserve registers
	push hl
	push de

	; Delete is equivalent to advancing the _cend pointer.
	ld hl, (_tb_end)
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
	ld de, (_tb_start)
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
	ld hl, (_tb_end)
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

	; Count the number of charcters before the next CR.
	ld bc, 0
	ld hl, (_cend)

	; Now we add the line to the line buffer. This is analgous to tb_putc.
	push hl
	ld hl, (_lcur)

	; Because we are using 3 bytes to store the chars in line count, we
 	; need to add 3 to the pointer.
	inc hl
	inc hl
	inc hl

	; Write 0 to the current line
	ld a, 0
	ld (hl), a

	; Now store the pointer back.
	ld (_lcur), hl

	; Increment the line count.
	ld hl, (_lcount)
	inc hl
	ld (_lcount), hl

	; We are done.
	pop hl
	ret
