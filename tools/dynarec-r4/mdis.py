#!/usr/bin/env python3
"""Minimal R3000 disassembler: mdis.py <ram.bin> <addr-hex> <count>"""
import struct,sys
R=['zero','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7',
   's0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
OPS={8:'addi',9:'addiu',10:'slti',11:'sltiu',12:'andi',13:'ori',14:'xori',15:'lui',
     32:'lb',33:'lh',34:'lwl',35:'lw',36:'lbu',37:'lhu',38:'lwr',40:'sb',41:'sh',42:'swl',43:'sw',46:'swr',50:'lwc2',58:'swc2',
     4:'beq',5:'bne',6:'blez',7:'bgtz',2:'j',3:'jal'}
FN={0:'sll',2:'srl',3:'sra',4:'sllv',6:'srlv',7:'srav',8:'jr',9:'jalr',12:'syscall',16:'mfhi',17:'mthi',18:'mflo',19:'mtlo',
    24:'mult',25:'multu',26:'div',27:'divu',32:'add',33:'addu',34:'sub',35:'subu',36:'and',37:'or',38:'xor',39:'nor',42:'slt',43:'sltu'}
def dis(w,pc):
    op=w>>26; rs=(w>>21)&31; rt=(w>>16)&31; rd=(w>>11)&31; sa=(w>>6)&31; imm=w&0xffff; simm=imm-((imm&0x8000)<<1)
    if w==0: return 'nop'
    if op==0:
        f=w&63; n=FN.get(f,'fn%d'%f)
        if f in (0,2,3): return '%s %s,%s,%d'%(n,R[rd],R[rt],sa)
        if f==8: return 'jr %s'%R[rs]
        if f in (24,25,26,27): return '%s %s,%s'%(n,R[rs],R[rt])
        if f in (16,18): return '%s %s'%(n,R[rd])
        return '%s %s,%s,%s'%(n,R[rd],R[rs],R[rt])
    if op==1: return '%s %s,%x'%('bgez' if rt&1 else 'bltz',R[rs],pc+4+simm*4)
    if op in (2,3): return '%s %08x'%(OPS[op],(pc&0xf0000000)|((w&0x3ffffff)<<2))
    if op in (4,5): return '%s %s,%s,%x'%(OPS[op],R[rs],R[rt],pc+4+simm*4)
    if op in (6,7): return '%s %s,%x'%(OPS[op],R[rs],pc+4+simm*4)
    if op==15: return 'lui %s,0x%x'%(R[rt],imm)
    if op in (32,33,34,35,36,37,38,40,41,42,43,46): return '%s %s,%d(%s)'%(OPS[op],R[rt],simm,R[rs])
    if op in (50,58): return '%s $%d,%d(%s)'%(OPS[op],rt,simm,R[rs])
    if op==18: return 'cop2 %07x'%(w&0x1ffffff) if w&(1<<25) else ['mfc2','?','cfc2','?','mtc2','?','ctc2'][rs]+' %s,$%d'%(R[rt],rd)
    if op==16: return 'cop0 rs%d %s,$%d'%(rs,R[rt],rd)
    if op in OPS: return '%s %s,%s,%d'%(OPS[op],R[rt],R[rs],simm)
    return '.word %08x'%w
if __name__=='__main__':
    ram=open(sys.argv[1],'rb').read(); a=int(sys.argv[2],16); n=int(sys.argv[3])
    for k in range(n):
        pc=a+4*k; w=struct.unpack_from('<I',ram,pc&0x1ffffc)[0]
        print('%08x %08x  %s'%(pc,w,dis(w,pc)))
