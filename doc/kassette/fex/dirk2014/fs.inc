;------------------------------------------------------------------------
;subroutine fcreat
;de=fcb
fcreat:	push	hl
	push	de
	push	bc
	call	resfcb		;bereinigen FCB
	ld	c,16h		;Anlegen Datei
	call	5
	pop	bc
	pop	de
	pop	hl
	ret
;------------------------------------------------------------------------
;subroutine fsave
;TODO konsolidieren mit epsave
;hl=Datenadresse
;de=fcb
;bc=Anzahl Bytes

fsave:	push	hl
	push	de
	push	bc

fsav1:	push	de
	ld	d,h
	ld	e,l
	call	setdma		;DMA-Adresse einstellen 
	pop	de
			
	call	fwrite		;Schreiben Block

	cp	00h
	jr	nz,fsav2	;error file write

	push	de
	ld	de,0080h
	add	hl,de		;dma=dma+0080h
	pop	de

	push	hl
	ld	h,b
	ld	l,c

	ld	bc,0080h
	scf
	ccf
	sbc	hl,bc		;Länge = länge - 0080h
	
	ld	b,h
	ld	c,l
	pop	hl

	ld	a,b
	or	c
	jr	z,fsavr		;ende bei 0000

	ld	a,b
	cp	0ffh
	jr	z,fsavr		;ende bei 0ffxx	

	jr	fsav1		;sprung, wenn size noch nicht <=0

fsavr:	pop	bc
	pop	de
	pop	hl
	ret
	
fsav2:	ld	a,0ffh		;error file write
	call	dispb
	jr	fsavr
;------------------------------------------------------------------------
;subroutine fclose
;de=fcb
;TODO konsolidieren mit epsave
fclose:	push	hl
	push	de
	push	bc
	ld	c,10h		;Schließen Datei
	call	5
	call	resfcb		;bereinigen FCB
	pop	bc
	pop	de
	pop	hl
	ret
;------------------------------------------------------------------------
;subroutine resfcb
;de=fcb
resfcb:	push	af
	push	hl
	push	de
	push	bc
	ld	hl,000ch	;1stes Byte nach Filename
	add	hl,de
	ld	d,h
	ld	e,l
	inc	de
	ld	bc,0018h	;Rest des FCB
	ld	(hl),0
	ldir			;clear fcb
	pop	bc
	pop	de
	pop	hl
	pop	af
	ret

;------------------------------------------------------------------------
;subroutine setdma
;de=dmaadr
setdma:	push	hl
	push	de
	push	bc
	ld	c,1ah		;DMA-Adresse einstellen
	call	5
	pop	bc
	pop	de
	pop	hl
	ret
;------------------------------------------------------------------------
;subroutine fwrite
;de=fcb
fwrite:	push	hl
	push	de
	push	bc
	ld	c,15h		;Schreiben Block
	call	5
	pop	bc
	pop	de
	pop	hl
	ret
	