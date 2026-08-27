OPTION CASEMAP:NONE
.code

PUBLIC StaticWdfPatternFixture
StaticWdfPatternFixture PROC
    db 048h,08Bh,005h,010h,000h,000h,000h
    db 048h,08Bh,004h,0C8h,0FFh,0D0h
    ret
StaticWdfPatternFixture ENDP

PUBLIC StaticCfgPatternFixture
StaticCfgPatternFixture PROC
    db 048h,08Bh,005h,010h,000h,000h,000h
    db 0FFh,0D0h
    ret
StaticCfgPatternFixture ENDP
END
