                        ; ===== RESET ENTRY POINT =====
  RESET_ENTRY:           0000  NOP                            
                        ; BC=0x0800 = 2048: clear 2KB RAM (both NOP/clear pass)
                         0001  LD BC,0800h                    
                         0004  LD D,C                         
                         0005  LD E,C                         
                         0006  LD H,C                         
                         0007  LD L,C                         
                        ; LDI loop: writes 0x00 (NOP) to RAM[0x0000..0x07FF]
  CLEAR_LOOP:            0008  LDI                            
                         000A  DEC HL                         
                         000B  JP PE,0008h                    
                        ; XOR A / OUT(02h): port 2 = bank select → map bank 0
  INIT_PORTS:            000E  XOR A                          
                         000F  OUT (02h),A                    
                        ; SP = 0x07E0 (stack below 0x0800)
                         0011  LD SP,07E0h                    
                        ; IM 2: interrupt mode 2, vectors at (I<<8)|vec
                         0014  IM 2                           
                        ; I = 0x00 → vector table at 0x0000
                         0016  LD A,00h                       
                         0018  LD I,A                         
                        ; ===== BS-PIO (port 0x08/09/0A/0B) INIT =====
  INIT_BS_PIO:           001A  LD A,7Fh                       
                        ; Port A: mode 1 (input)
                         001C  OUT (09h),A                    
                        ; Port B: mode 1 (input)
                         001E  OUT (0Bh),A                    
                         0020  LD A,FFh                       
                        ; Port A data = 0xFF
                         0022  OUT (08h),A                    
                        ; Port B data = 0xFF
                         0024  OUT (0Ah),A                    
                         0026  LD A,B8h                       
                        ; Port A interrupt VECTOR = 0xB8  → ISR=RAM[0xB8,0xB9]
                         0028  OUT (09h),A                    
                         002A  LD A,FFh                       
                        ; Port A: mode 3 (bit control)
                         002C  OUT (09h),A                    
                         002E  LD A,7Fh                       
                        ; Port A mode-3 I/O mask = 0x7F (bit7=output, bits6..0=input)
                         0030  OUT (09h),A                    
                        ; ===== RAM TEST / BANK WALK =====
  RAM_TEST:              0032  LD IX,0800h                    
                        ; save IX to RAM[0x0462]
                         0036  LD (0462h),IX                  
                         003A  LD HL,044Eh                    
                        ; HL=0x044E (config flag addr), DE=0x0400, B=0x3E (62 iterations)
                         003D  LD DE,0400h                    
                         0040  LD B,3Eh                       
                        ; C = RAM[IX+0] (read before write)
  RAM_TEST_LOOP:         0042  LD C,(IX+0)                    
                         0045  LD A,D7h                       
                        ; int ctrl: INTENA=1, OR, H-level, mask follows
                         0047  OUT (09h),A                    
                         0049  LD A,9Fh                       
                        ; interrupt mask 0x9F
                         004B  OUT (09h),A                    
                        ; EI: enable interrupts during RAM write
                         004D  EI                             
                        ; write 0xFF to RAM[IX] — if BS-PIO /ASTB fires, interrupt 0xB8
                         004E  LD (IX+0),FFh                  
                         0052  NOP                            
                        ; DI: disable interrupts
                         0053  DI                             
                        ; disable Port A interrupt (INTENA=0)
                         0054  LD A,47h                       
                         0056  OUT (09h),A                    
                        ; ADD IX,DE (IX += 0x0400 → next 1KB bank)
                         0058  ADD IX,DE                      
                         005A  DJNZ 0042h                     
                        ; LDDR: copy 0x24 bytes from ROM[0x046F..0x044C] → RAM[0x006F..0x004C]
  COPY_DISK_TBL:         005C  LD HL,046Fh                    
                         005F  LD DE,006Fh                    
                         0062  LD BC,0024h                    
                         0065  LDDR                           
                        ; HL=0x044E: read config byte (= 0x00 after RAM clear)
  CHECK_BOOT_FLAG:       0067  LD HL,044Eh                    
                        ; BIT 0,(HL): test 'already booted' flag
                         006A  BIT 0,(HL)                     
                        ; JR Z → ROM_COPY (flag=0 means first boot → do copy)
                         006C  JR Z,00BCh                     
                        ; flag=1: skip ROM copy, jump via IX to main
  SKIP_ROM_COPY:         006E  LD HL,0800h                    
                         0071  LD (004Ch),HL                  
                         0074  LD HL,(0462h)                  
                         0077  LD L,9Dh                       
                         0079  JP (HL)                        
  ISR_BS_PIO_A:          007A  LD A,47h                       
                         007C  OUT (09h),A                    
                         007E  LD A,FFh                       
                         0080  CP (IX+0)                      
                         0083  JR NZ,00A7h                    
                         0085  LD (IX+0),C                    
                         0088  BIT 0,(HL)                     
                         008A  JR NZ,0092h                    
                         008C  LD (0460h),IX                  
                         0090  RETI                           
                         0092  LD (0464h),IX                  
                         0096  BIT 6,(HL)                     
                         0098  JR NZ,00A5h                    
                         009A  SET 6,(HL)                     
                         009C  LD (046Ch),IX                  
                         00A0  LD A,05h                       
                         00A2  LD (046Ch),A                   
                         00A5  RETI                           
                         00A7  BIT 0,(HL)                     
                         00A9  JR NZ,00B1h                    
                         00AB  SET 0,(HL)                     
                         00AD  LD (0462h),IX                  
                         00B1  LD (0464h),IX                  
                         00B5  RETI                           
  ROM_DATA_BA_BB:        00B7  NOP                            
                         00B8  LD A,D                         
                         00B9  NOP                            
                         00BA  RST 00h                        
                         00BB  LD BC,BA21h                    
                         00BE  NOP                            
                        ; DE = HL = 0x00BA: dest = RAM[0x00BA]
                         00BF  LD D,H                         
                         00C0  LD E,L                         
                        ; BC = 0x0346 = 838 bytes → copies ROM[0x00BA..0x03FF] → RAM[0x00BA..0x03FF]
                         00C1  LD BC,0346h                    
                        ; After LDIR: RAM[0x00BA]=0xC7, RAM[0x00BB]=0x01 → ISR for vec 0xBA = 0x01C7
                         00C4  LDIR                           
                        ; ===== POST-COPY HARDWARE INIT =====
  POST_COPY_INIT:        00C6  LD A,FFh                       
                        ; Port B: mode 3
                         00C8  OUT (0Bh),A                    
                        ; port 0x13 (K5122 ctrl PIO Port B ctrl) = 0xFF → mode 3
                         00CA  OUT (13h),A                    
                         00CC  LD A,E2h                       
                        ; Port B interrupt vector = 0xE2
                         00CE  OUT (0Bh),A                    
                        ; read Port B data (hardware config bits)
                         00D0  IN A,(0Ah)                     
                        ; AND 0xF6 = ~0x09: clears bits 3 and 0
                         00D2  AND F6h                        
                        ; OUT(0Ah): WRITE BACK → bit0 low = ROM DISABLE
                         00D4  OUT (0Ah),A                    
                        ; EI: interrupts enabled (ROM disabled, now running from RAM)
  ENABLE_INTS_INIT:      00D6  EI                             
                         00D7  LD A,F3h                       
                        ; port 0x13 (K5122 ctrl PIO Port B ctrl): F3h = 1111 0011
                         00D9  OUT (13h),A                    
                         00DB  LD A,7Fh                       
                        ; port 0x12 (K5122 ctrl PIO Port B data): 7Fh
                         00DD  OUT (12h),A                    
                        ; port 0x11 (K5122 ctrl PIO Port A ctrl): mode 1 (input)  [0x7F]
                         00DF  OUT (11h),A                    
                         00E1  LD A,A9h                       
                        ; port 0x10 (K5122 ctrl PIO Port A data): A9h = 1010 1001 → drive control
                         00E3  OUT (10h),A                    
                         00E5  LD A,3Fh                       
                        ; port 0x11: 0x3F = mode 0 (output) for ctrl PIO Port A
                         00E7  OUT (11h),A                    
                        ; port 0x15 (K5122 data PIO Port A ctrl): 0x3F = mode 0
                         00E9  OUT (15h),A                    
                        ; *** K5122 ctrl PIO Port A: INTERRUPT VECTOR = 0xBA ***
                         00EB  LD A,BAh                       
                        ; → when /ASTB fires (index pulse), ISR = RAM[0xBA,0xBB] = 0x01C7
                         00ED  OUT (11h),A                    
                        ; RAM[0x07F2] = 0x04 (seek retry count?)
                         00EF  LD A,04h                       
                         00F1  LD (07F2h),A                   
                        ; RAM[0x0000] = 0xC3 (JP opcode)
                         00F4  LD A,C3h                       
                         00F6  LD (0000h),A                   
                        ; RAM[0x0001..0x0002] = 0x01DD → JP 0x01DDh (strobe loop entry)
                         00F9  LD HL,01DDh                    
                         00FC  LD (0001h),HL                  
                        ; RAM[0x03FC] = 0xEE (initial ctrl byte)
                         00FF  LD A,EEh                       
                         0101  LD (03FCh),A                   
                        ; port 0x18 (8212 drive select): EEh → no drive selected
                         0104  OUT (18h),A                    
                        ; HL = 0x0400: load address
                         0106  LD HL,0400h                    
                        ; B = L = 0x00
                         0109  LD B,L                         
                        ; RAM[0x03F0] = 0x0400 (load address stored)
                         010A  LD (03F0h),HL                  
                        ; DE = 0x5006: D=retry count, E=0x06
                         010D  LD DE,5006h                    
                        ; IN A,(12h): read K5122 ctrl PIO Port B (drive status)
  DRIVE_DETECT_LOOP:     0110  IN A,(12h)                     
                        ; RLCA (opcode 0x07): rotate A left through carry
                         0112  RLCA                           
                        ; JR NC,0x0128: if bit7 was 0 (/RDYL=ready) → drive found → proceed
                         0113  JR NC,0128h                    
                        ; DEC D: decrement outer retry
                         0115  DEC D                          
                        ; JR Z,0x0140: retries exhausted → boot fail
                         0116  JR Z,0140h                     
                        ; send seek-to-track-0 pulse via port 0x10
  SEND_SEEK_TRACK0:      0118  LD A,09h                       
                         011A  LD B,A                         
                         011B  OUT (10h),A                    
                         011D  LD A,89h                       
                         011F  OUT (10h),A                    
                         0121  DEC C                          
                         0122  JR NZ,0121h                    
                         0124  DJNZ 0121h                     
                         0126  JR 0110h                       
                        ; HL = 0x03F7, [0x03F7] = 4  (pulse counter)
  WAIT_INDEX_SETUP:      0128  LD HL,03F7h                    
                         012B  LD (HL),04h                    
                        ;   INTENA=1, OR mode, H-level, mask follows
                         012D  LD A,97h                       
                         012F  OUT (11h),A                    
                        ; interrupt mask = 0xFF (in mode 0, mask is ignored; IE=1 is what matters)
                         0131  LD A,FFh                       
                         0133  OUT (11h),A                    
                        ; A = [0x03F7]  (decremented by ISR at 0x01C7 on each index pulse)
  WAIT_INDEX_LOOP:       0135  LD A,(HL)                      
                         0136  OR A                           
                        ; A = 0x87 (preload value for [0x03FD])
                         0137  LD A,87h                       
                        ; JR Z: if [0x03F7]=0 → 4 pulses received → jump to STORE_03FD
                         0139  JR Z,0153h                     
                        ; DEC C / JR NZ: inner timeout
                         013B  DEC C                          
  WAIT_INDEX_INNER:      013C  JR NZ,0135h                    
                         013E  DJNZ 0135h                     
                        ; ===== BOOT FAILURE (timeout) =====
  BOOT_FAIL:             0140  LD A,(03FCh)                   
                         0143  CP 77h                         
                         0145  JP Z,027Eh                     
                         0148  RLCA                           
                         0149  JR 0101h                       
                         014B  DEC E                          
                         014C  JR Z,0140h                     
  WAIT_INDEX_OK:         014E  LD A,(03FDh)                   
                         0151  XOR 02h                        
                        ; [0x03FD] = 0x87: bit1=1 → NZ path in IDAM loop; bit7=1 → no step pulse
  STORE_03FD:            0153  LD (03FDh),A                   
                         0156  LD HL,8001h                    
                         0159  LD (07F0h),HL                  
  SEEK_SETUP:            015C  LD H,00h                       
                         015E  LD (03F5h),HL                  
                         0161  LD L,H                         
                         0162  LD (03F3h),HL                  
                         0165  CALL 0194h                     
                         0168  LD A,(HL)                      
                         0169  OR A                           
                         016A  JR Z,0168h                     
  STROBE_INIT:           016C  DEC A                          
                         016D  JR Z,014Bh                     
                         016F  CALL 01B6h                     
                         0172  JR NZ,0140h                    
                         0174  CALL 0437h                     
                         0177  LD HL,(03FAh)                  
                         017A  LD (0462h),HL                  
                         017D  IN A,(0Ah)                     
                         017F  OR 01h                         
                         0181  OUT (0Ah),A                    
                         0183  LD BC,0346h                    
                         0186  LD DE,00BAh                    
                         0189  LD H,D                         
                         018A  LD L,H                         
                         018B  LDI                            
                         018D  DEC HL                         
                         018E  JP PE,018Bh                    
                         0191  JP 0074h                       
                         0194  LD HL,03F7h                    
                         0197  LD (HL),10h                    
                         0199  OUT (14h),A                    
                         019B  OUT (04h),A                    
                         019D  LD A,FFh                       
                         019F  OUT (17h),A                    
                         01A1  OUT (17h),A                    
                         01A3  LD A,A5h                       
                         01A5  OUT (10h),A                    
                         01A7  LD A,7Fh                       
                         01A9  OUT (17h),A                    
                         01AB  IN A,(16h)                     
                         01AD  LD A,(03FDh)                   
                         01B0  OUT (10h),A                    
                         01B2  INC HL                         
                         01B3  LD (HL),00h                    
                         01B5  RET                            
                         01B6  LD HL,(0400h)                  
                         01B9  LD BC,5953h                    
                         01BC  SBC HL,BC                      
                         01BE  RET NZ                         
                         01BF  LD HL,0402h                    
                         01C2  LD A,4Ch                       
                         01C4  CP (HL)                        
                         01C5  RET NZ                         
                         01C6  RET                            
                        ; Entry: interrupt vector 0xBA → RAM[0xBA,0xBB] = {0xC7,0x01} = 0x01C7
  ISR_DECREMENT_03F7:    01C7  EI                             
                         01C8  PUSH AF                        
                         01C9  PUSH HL                        
                         01CA  LD HL,03F7h                    
                        ; DEC (HL): [0x03F7]--
                         01CD  DEC (HL)                       
                        ; JR Z: if 0 → assert /STR, set [0x03F8]=1
                         01CE  JR Z,01D4h                     
                         01D0  POP HL                         
                         01D1  POP AF                         
                         01D2  RETI                           
                        ; LD A,0xBD / OUT(10h): assert /STR (bit3 low = strobe pulse)
  ISR_ASSERT_STR:        01D4  LD A,BDh                       
                         01D6  OUT (10h),A                    
                        ; [0x03F8] = 0x01 (strobe fired flag)
                         01D8  INC HL                         
                         01D9  LD (HL),01h                    
                         01DB  JR 01D0h                       
                        ; ===== MAIN STROBE / IDAM SEARCH LOOP =====
  STROBE_LOOP:           01DD  LD HL,(03F0h)                  
                         01E0  LD A,(07F1h)                   
                         01E3  LD B,A                         
                         01E4  LD DE,0700h                    
                         01E7  LD C,16h                       
                        ; Load IDAM comparison registers (alternate register set):
  LOAD_IDAM_REGS:        01E9  EXX                            
                        ; B'=0xFE (IDAM marker), C'=0xA1
                         01EA  LD BC,FEA1h                    
                        ; D'=[0x03F4] (head), E'=[0x03F3] (cylinder)
                         01ED  LD DE,(03F3h)                  
                        ; H'=[0x03F6] (size_code), L'=[0x03F5] (sector_id)
                         01F1  LD HL,(03F5h)                  
                         01F4  LD A,BDh                       
                         01F6  OUT (10h),A                    
                        ; LD A,0xB5 / OUT(10h): /STR low (bit3 falling edge → doReadSector())
  ASSERT_STR:            01F8  LD A,B5h                       
                         01FA  OUT (10h),A                    
                        ; JR → STROBE_LOOP_BODY
                         01FC  JR 025Ah                       
                         01FE  LD H,A                         
                         01FF  XOR A                          
                         0200  EXX                            
                         0201  LD B,A                         
                         0202  EXX                            
                         0203  JR 0273h                       
                        ; Z path (bit1=0 of [0x03FD]): read 5 bytes before IDAM compare
  READ_IDAM_Z:           0205  IN A,(16h)                     
                         0207  IN A,(16h)                     
                         0209  IN A,(16h)                     
                        ; NZ path (bit1=1 of [0x03FD]): read 2 bytes before IDAM compare
  READ_IDAM_NZ:          020B  IN A,(16h)                     
                        ; CP B: compare read byte with B=0xFE (IDAM mark)
  CMP_IDAM_MARK:         020D  CP B                           
                         020E  JR NZ,01F8h                    
                         0210  IN A,(16h)                     
                         0212  CP E                           
                         0213  JR NZ,01F8h                    
                         0215  IN A,(16h)                     
                         0217  CP D                           
                         0218  JR NZ,01F8h                    
  IDAM_MATCH:            021A  IN A,(16h)                     
                         021C  CP L                           
                         021D  JR NZ,01F8h                    
                         021F  IN A,(16h)                     
                         0221  CP H                           
                         0222  JR NZ,01FEh                    
                         0224  LD A,B5h                       
                         0226  OUT (10h),A                    
                         0228  LD A,(03FDh)                   
                         022B  BIT 1,A                        
                         022D  OUT (10h),A                    
                         022F  IN A,(16h)                     
                         0231  JR NZ,0239h                    
                         0233  IN A,(16h)                     
                         0235  IN A,(16h)                     
                         0237  IN A,(16h)                     
                         0239  IN A,(16h)                     
                         023B  EXX                            
                         023C  DB EDh,A2h                     
                         023E  DB EDh,A2h                     
                         0240  DB EDh,A2h                     
                         0242  DB EDh,B2h                     
                         0244  EX DE,HL                       
                         0245  DB EDh,A2h                     
                         0247  DB EDh,A2h                     
                         0249  LD A,B5h                       
                         024B  OUT (10h),A                    
                         024D  EX DE,HL                       
                         024E  LD A,(07F1h)                   
                         0251  LD B,A                         
                         0252  EXX                            
                         0253  LD A,(07F2h)                   
                         0256  CP L                           
                         0257  JR Z,0267h                     
                         0259  INC L                          
                        ; A = [0x03FD]: path/step config byte
  STROBE_LOOP_BODY:      025A  LD A,(03FDh)                   
                        ; BIT 1,A: test NZ path bit
                         025D  BIT 1,A                        
                        ; OUT(10h),A: DANGER if bit7=0: causes step pulse!
                         025F  OUT (10h),A                    
                         0261  IN A,(16h)                     
                         0263  JR Z,0205h                     
                         0265  JR 020Bh                       
                         0267  LD A,03h                       
                         0269  OUT (11h),A                    
                         026B  LD (03F8h),A                   
                         026E  JR 0265h                       
                         0270  LD BC,1EDAh                    
                         0273  LD (07F1h),A                   
                         0276  LD A,02h                       
                         0278  LD (07F2h),A                   
                         027B  JP 01F8h                       
                         027E  LD A,FFh                       
                         0280  OUT (18h),A                    
                         0282  LD HL,03BCh                    
                         0285  LD (03F0h),HL                  
                         0288  LD HL,03D4h                    
                         028B  LD (03F2h),HL                  
                         028E  LD HL,03C7h                    
                         0291  LD (03F4h),HL                  
                         0294  LD A,03h                       
                         0296  LD I,A                         
                         0298  LD A,7Fh                       
                         029A  OUT (33h),A                    
                         029C  OUT (36h),A                    
                         029E  OUT (37h),A                    
                         02A0  LD A,03h                       
                         02A2  OUT (34h),A                    
                         02A4  INC A                          
                         02A5  OUT (35h),A                    
                         02A7  LD A,FFh                       
                         02A9  OUT (37h),A                    
                         02AB  LD A,FDh                       
                         02AD  OUT (37h),A                    
                         02AF  LD A,FFh                       
                         02B1  OUT (36h),A                    
                         02B3  LD A,80h                       
                         02B5  OUT (36h),A                    
                         02B7  IN A,(34h)                     
                         02B9  AND 7Ch                        
                         02BB  JP NZ,03B0h                    
                         02BE  LD E,09h                       
                         02C0  LD A,F2h                       
                        ; ===== CTC INIT (late, only reached after boot) =====
  INIT_CTC:              02C2  OUT (0Ch),A                    
                         02C4  LD A,05h                       
                         02C6  LD (03FAh),A                   
                         02C9  LD A,E                         
                         02CA  OUT (34h),A                    
                         02CC  LD B,00h                       
                         02CE  CALL 03B6h                     
                         02D1  CALL 03B6h                     
                         02D4  LD HL,0400h                    
                         02D7  LD (03F8h),HL                  
                         02DA  LD D,10h                       
                         02DC  IN A,(35h)                     
                         02DE  BIT 0,E                        
                         02E0  JR NZ,02E4h                    
                         02E2  LD D,08h                       
                         02E4  AND D                          
                         02E5  JP Z,0397h                     
                         02E8  XOR A                          
                         02E9  LD (03F6h),A                   
                         02EC  LD A,14h                       
                         02EE  LD (03F7h),A                   
                         02F1  LD A,A7h                       
                        ; CTC channel 1 (port 0x0D) configuration
  INIT_CTC2:             02F3  OUT (0Dh),A                    
                         02F5  LD A,FFh                       
                         02F7  OUT (0Dh),A                    
                         02F9  IN A,(34h)                     
                         02FB  BIT 7,A                        
                         02FD  JR NZ,0317h                    
                         02FF  OR 20h                         
                         0301  OUT (34h),A                    
                         0303  LD L,03h                       
                         0305  CALL 03B6h                     
                         0308  IN A,(34h)                     
                         030A  BIT 7,A                        
                         030C  JR NZ,0317h                    
                         030E  DEC L                          
                         030F  JR NZ,0305h                    
                         0311  AND DFh                        
                         0313  OUT (34h),A                    
                         0315  JR 033Bh                       
                         0317  AND DFh                        
                         0319  OUT (34h),A                    
                         031B  CALL 03B6h                     
                         031E  OR 30h                         
                         0320  OUT (34h),A                    
                         0322  AND C3h                        
                         0324  OUT (34h),A                    
                         0326  IN A,(35h)                     
                         0328  AND D                          
                         0329  JR Z,0326h                     
                         032B  IN A,(34h)                     
                         032D  BIT 0,A                        
                         032F  JR NZ,0335h                    
                         0331  ADD A,04h                      
                         0333  JR 0337h                       
                         0335  ADD A,08h                      
                         0337  OUT (34h),A                    
                         0339  JR 02F9h                       
                         033B  LD C,A                         
                         033C  AND F3h                        
                         033E  OR 40h                         
                         0340  OUT (34h),A                    
                         0342  LD A,C                         
                         0343  OUT (34h),A                    
                         0345  OR 10h                         
                         0347  OUT (34h),A                    
                         0349  IN A,(34h)                     
                         034B  RLA                            
                         034C  JR NC,0349h                    
                         034E  LD B,00h                       
                         0350  CALL 03B6h                     
                         0353  LD D,04h                       
                         0355  LD A,F0h                       
                         0357  OUT (33h),A                    
                         0359  LD BC,8331h                    
                         035C  LD A,97h                       
                         035E  OUT (33h),A                    
                         0360  OUT (33h),A                    
                         0362  IN A,(31h)                     
                         0364  XOR A                          
                         0365  CP B                           
                         0366  JR NZ,0365h                    
                         0368  LD A,03h                       
                         036A  OUT (33h),A                    
                         036C  LD HL,(03F8h)                  
                         036F  DEC HL                         
                         0370  DEC HL                         
                         0371  DEC HL                         
                         0372  LD (03F8h),HL                  
                         0375  DEC D                          
                         0376  JR NZ,0355h                    
                         0378  LD BC,0D00h                    
                         037B  CALL 03B6h                     
                         037E  IN A,(34h)                     
                         0380  AND EFh                        
                         0382  OUT (34h),A                    
                         0384  LD A,21h                       
                         0386  OUT (0Dh),A                    
                         0388  CALL 01B6h                     
                         038B  JR NZ,0398h                    
                         038D  CALL 0434h                     
                         0390  JP 0177h                       
                         0393  LD A,03h                       
                         0395  OUT (33h),A                    
                         0397  NOP                            
                         0398  LD A,21h                       
                         039A  OUT (0Dh),A                    
                         039C  LD HL,03FAh                    
                         039F  DEC (HL)                       
                         03A0  LD A,03h                       
                         03A2  OUT (34h),A                    
                         03A4  JP NZ,02C9h                    
                         03A7  BIT 0,E                        
                         03A9  JR Z,03B0h                     
                         03AB  LD E,06h                       
                         03AD  JP 02C4h                       
                         03B0  LD A,9Fh                       
                         03B2  OUT (03h),A                    
                         03B4  JR 03B4h                       
                         03B6  DEC C                          
                         03B7  JR NZ,03B6h                    
                         03B9  DJNZ 03B6h                     
                         03BB  RET                            
                         03BC  EI                             
                         03BD  PUSH AF                        
                         03BE  IN A,(31h)                     
                         03C0  LD A,F4h                       
                         03C2  OUT (33h),A                    
                         03C4  POP AF                         
                         03C5  RETI                           
                         03C7  EI                             
                         03C8  PUSH AF                        
                         03C9  PUSH HL                        
                         03CA  LD HL,(03F8h)                  
                         03CD  DB EDh,A2h                     
                         03CF  LD (03F8h),HL                  
                         03D2  JR 03E1h                       
                         03D4  EI                             
                         03D5  PUSH AF                        
                         03D6  PUSH HL                        
                         03D7  LD HL,03F6h                    
                         03DA  DEC (HL)                       
                         03DB  JR NZ,03E1h                    
                         03DD  INC L                          
                         03DE  DEC (HL)                       
                         03DF  JR Z,03E5h                     
                         03E1  POP HL                         
                         03E2  POP AF                         
                         03E3  RETI                           
                         03E5  POP HL                         
                         03E6  POP AF                         
                         03E7  POP AF                         
                         03E8  LD HL,0393h                    
                         03EB  EX (SP),HL                     
                         03EC  PUSH AF                        
                         03ED  RETI                           
                         03EF  NOP                            
                         03F0  NOP                            
                         03F1  INC B                          
                         03F2  CP 00h                         
                         03F4  NOP                            
                         03F5  NOP                            
                         03F6  NOP                            
                         03F7  NOP                            
                         03F8  NOP                            
                         03F9  NOP                            
                         03FA  NOP                            
                         03FB  EX AF,AF'                      
                         03FC  NOP                            
                         03FD  NOP                            
                         03FE  RST 30h                        
                         03FF  LD L,E                         
