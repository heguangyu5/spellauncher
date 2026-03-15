.section .data

/* font */
.align 4
.global chinese_font_start
.global chinese_font_end
chinese_font_start:
.incbin "resources/SourceHanSansSC-Bold.ttf"
chinese_font_end:

/* type.wav */
.align 4
.global type_wav_start
.global type_wav_end
type_wav_start:
.incbin "resources/type.wav"
type_wav_end:

/* error.wav */
.align 4
.global error_wav_start
.global error_wav_end
error_wav_start:
.incbin "resources/error.wav"
error_wav_end:

/* success.wav */
.align 4
.global success_wav_start
.global success_wav_end
success_wav_start:
.incbin "resources/success.wav"
success_wav_end:

#ifdef __ELF__
.section .note.GNU-stack,"",@progbits
#endif
