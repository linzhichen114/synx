.global enter_user
.type enter_user, @function

enter_user:
	pushq $0x1b
	pushq %rsi
	pushq $0x202
	pushq $0x23
	pushq %rdi
	iretq

.size enter_user, .-enter_user