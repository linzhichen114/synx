.global enter_user
.type enter_user, @function

enter_user:
	pushq $0x23
	pushq %rsi
	pushq $0x2
	pushq $0x1b
	pushq %rdi
	iretq

.size enter_user, .-enter_user