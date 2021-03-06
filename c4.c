// c4.c - C in four functions

// char, int, and pointer types
// if, while, return, and expression statements
// just enough features to allow self-compilation and a bit more

// Written by Robert Swierczek

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <fcntl.h>
#define int long long // 兼容64位系统

char *p, *lp, // current position in source code
     *data;   // data/bss pointer

int *e, *le,  // current position in emitted code
    *id,      // currently parsed identifier
    *sym,     // symbol table (simple list of identifiers)
    tk,       // current token
    ival,     // current token value
    ty,       // current expression type
    loc,      // local variable offset
    line,     // current line number
    src,      // print source and assembly flag
    debug;    // print executed instructions

// tokens and classes (operators last and in precedence order)
enum {
  Num = 128, Fun, Sys, Glo, Loc, Id,
  Char, Else, Enum, If, Int, Return, Sizeof, While,
  Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt, Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak
};

// opcodes
enum { LEA ,IMM ,JMP ,JSR ,BZ  ,BNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PSH ,
       OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,
       OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT };

// types
enum { CHAR, INT, PTR };

// identifier offsets (since we can't create an ident struct)
enum { Tk, Hash, Name, Class, Type, Val, HClass, HType, HVal, Idsz };

void next()
{
  char *pp;

  while (tk = *p) {
    ++p;
    if (tk == '\n') {
      if (src) {
        // 先把源码打印出来，包含被注释的部分，例如
        // 16: int main(int argc, char **argv)
        // 17: {
        // 18:   //int a, b;
        // 19:   return 0;
        //     ENT  0
        //     IMM  0
        //     LEV
        printf("%d: %.*s", line, p - lp, lp);
        lp = p;
        while (le < e) {
          // 取如下字符串初始内存地址作为数组，数组长度位5个char，le地址存的是opcode地址，取值乘以5得到的是对应的字符串
          // 例如 ENT 0
          printf("%8.4s", &"LEA ,IMM ,JMP ,JSR ,BZ  ,BNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PSH ,"
                           "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
                           "OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT,"[*++le * 5]);
          // opcode小于等于ADJ的都是带一个参数，例如IMM 0  
          if (*le <= ADJ) printf(" %d\n", *++le); else printf("\n");
        }
      }
      ++line;
    }
    else if (tk == '#') { // 并不支持宏和头文件引用
      while (*p != 0 && *p != '\n') ++p;
    }
    else if ((tk >= 'a' && tk <= 'z') || (tk >= 'A' && tk <= 'Z') || tk == '_') {
      pp = p - 1;
      while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')
        tk = tk * 147 + *p++; // 计算哈希
      tk = (tk << 6) + (p - pp); // 得到最终的hash，低位存的是p - pp也就是字符串的长度   
      id = sym;
      while (id[Tk]) { // 老派程序设计思想，一切基于内存取值，Idsz为id数据结构的长度，以此进行遍历
        if (tk == id[Hash] && !memcmp((char *)id[Name], pp, p - pp)) { tk = id[Tk]; return; }
        id = id + Idsz;
      }
      id[Name] = (int)pp; // pp存的是字符串基地址
      id[Hash] = tk;
      tk = id[Tk] = Id;
      return;
    }
    else if (tk >= '0' && tk <= '9') {
      if (ival = tk - '0') { while (*p >= '0' && *p <= '9') ival = ival * 10 + *p++ - '0'; }
      else if (*p == 'x' || *p == 'X') { // 处理16进制
        while ((tk = *++p) && ((tk >= '0' && tk <= '9') || (tk >= 'a' && tk <= 'f') || (tk >= 'A' && tk <= 'F')))
          ival = ival * 16 + (tk & 15) + (tk >= 'A' ? 9 : 0);
      }
      else { while (*p >= '0' && *p <= '7') ival = ival * 8 + *p++ - '0'; } // 处理8进制
      tk = Num;
      return;
    }
    else if (tk == '/') { // 不支持 /**/ 格式的多行注释
      if (*p == '/') {
        ++p;
        while (*p != 0 && *p != '\n') ++p;
      }
      else {
        tk = Div;
        return;
      }
    }
    else if (tk == '\'' || tk == '"') {
      pp = data;
      while (*p != 0 && *p != tk) {
        if ((ival = *p++) == '\\') {
          if ((ival = *p++) == 'n') ival = '\n'; // 只处理\n转义，其他转义直接过滤掉，比如\t直接变为t
        }
        if (tk == '"') *data++ = ival; // 把每个ival字符传入到data中
      }
      ++p;
      if (tk == '"') ival = (int)pp; else tk = Num; // 如果是字符串，设置data字符串首地址给ival
      return;
    }
    else if (tk == '=') { if (*p == '=') { ++p; tk = Eq; } else tk = Assign; return; }
    else if (tk == '+') { if (*p == '+') { ++p; tk = Inc; } else tk = Add; return; }
    else if (tk == '-') { if (*p == '-') { ++p; tk = Dec; } else tk = Sub; return; }
    else if (tk == '!') { if (*p == '=') { ++p; tk = Ne; } return; }
    else if (tk == '<') { if (*p == '=') { ++p; tk = Le; } else if (*p == '<') { ++p; tk = Shl; } else tk = Lt; return; }
    else if (tk == '>') { if (*p == '=') { ++p; tk = Ge; } else if (*p == '>') { ++p; tk = Shr; } else tk = Gt; return; }
    else if (tk == '|') { if (*p == '|') { ++p; tk = Lor; } else tk = Or; return; }
    else if (tk == '&') { if (*p == '&') { ++p; tk = Lan; } else tk = And; return; }
    else if (tk == '^') { tk = Xor; return; }
    else if (tk == '%') { tk = Mod; return; }
    else if (tk == '*') { tk = Mul; return; }
    else if (tk == '[') { tk = Brak; return; }
    else if (tk == '?') { tk = Cond; return; }
    else if (tk == '~' || tk == ';' || tk == '{' || tk == '}' || tk == '(' || tk == ')' || tk == ']' || tk == ',' || tk == ':') return;
  }
}

void expr(int lev)
{
  int t, *d;
  // e指向的是代码段，当然这里代码段都是存在内存里
  // expr先处理的是表达式的左部分，右部分放在了后面的while(tk >= lev)循环里
  if (!tk) { printf("%d: unexpected eof in expression\n", line); exit(-1); }
  else if (tk == Num) { *++e = IMM; *++e = ival; next(); ty = INT; } // IMM 123 type:INT
  else if (tk == '"') { // 处理字符串 IMM ival type:PTR
    *++e = IMM; *++e = ival; next();
    while (tk == '"') next(); // 如果是连续的多个字符串，继续往后处理并进行拼接
    data = (char *)((int)data + sizeof(int) & -sizeof(int)); ty = PTR; // 由于data是一个个char传入的，这里做指针对齐
  }
  else if (tk == Sizeof) { // 支持int char (int *) (char *) (int **) (char **) ... int的长度是64位 long long
    next(); if (tk == '(') next(); else { printf("%d: open paren expected in sizeof\n", line); exit(-1); }
    ty = INT; if (tk == Int) next(); else if (tk == Char) { next(); ty = CHAR; }
    while (tk == Mul) { next(); ty = ty + PTR; } // 老派程序设计思想，这里可以很好处理指针递归，例如(int *) (int **)
                                                 // type本身是INT或CHAR，加上PTR分别指代(INT *)和(CHAR *)
                                                 // 如果是(int **)，则type加上两次PTR
    if (tk == ')') next(); else { printf("%d: close paren expected in sizeof\n", line); exit(-1); }
    *++e = IMM; *++e = (ty == CHAR) ? sizeof(char) : sizeof(int); // int (int *) (char *) ... 作为sizeof(int)处理
    ty = INT;
  }
  else if (tk == Id) { // 处理定义的变量和函数
    d = id; next();
    if (tk == '(') {
      next();
      t = 0;
      while (tk != ')') { 
        expr(Assign); // 获取函数参数，Assign的含义是，后续如果符号优先级大于等于Assign，则可以进行表达式运算
                      // 优先级爬坡，下面while (tk >= lev)会提到，这是一种递归的算法，最终递归终止于最高优先级运算
                      // 比如第一个参数如果是: sizeof(int) + 4，会递归调用先处理sizeof(int)，然后next token是 + 号，
                      // 优先级高于assign，递归获得下一个num 4，4后面是,逗号或者)右括号，退出递归调用
        *++e = PSH; ++t; // 把参数PUSH入栈
        //if (tk == ',') next(); // 这里有bug，如果参数之间没有逗号，也不会报错，如下两行fix了这个问题
        if (tk == ',') { next(); if(tk == ')') { printf("%d: error unexpected comma in function call\n", line); exit(-1); }}
        else if(tk != ')') { printf("%d: error missing comma in function call\n", line); exit(-1); }
      }
      next();
      if (d[Class] == Sys) *++e = d[Val]; // 如果是系统调用，d[Val]指代的是opcode，例如printf是PRTF
      else if (d[Class] == Fun) { *++e = JSR; *++e = d[Val]; } // 如果是函数调用，JSR d[Val]，d[Val]是函数入口地址
      else { printf("%d: bad function call\n", line); exit(-1); }
      if (t) { *++e = ADJ; *++e = t; } // 函数调用完毕后，需要出栈 ADJ t，t是参数个数
      ty = d[Type]; 
    }
    else if (d[Class] == Num) { *++e = IMM; *++e = d[Val]; ty = INT; } // Enum类型，IMM d[Val] type: INT
                                                                       // 参考main函数的Enum处理部分
    else {
      if (d[Class] == Loc) { *++e = LEA; *++e = loc - d[Val]; } // 本地变量 loc - d[Val]，得到是变量的相对位置，
                                                                // 函数参数是从2开始递增，局部变量从-1开始递减
                                                                // 具体栈帧结构参考main函数中的说明，loc的处理也在
                                                                // main函数中
      else if (d[Class] == Glo) { *++e = IMM; *++e = d[Val]; } // 全局变量
      else { printf("%d: undefined variable\n", line); exit(-1); }
      *++e = ((ty = d[Type]) == CHAR) ? LC : LI; // LC LI load指令，从寄存器取指针指向的值，reg = *(reg)
    }
  }
  else if (tk == '(') {
    next();
    if (tk == Int || tk == Char) { // 类型转换，包含 int char (int *) (char *) (int **) (char **) ...
      t = (tk == Int) ? INT : CHAR; next();
      while (tk == Mul) { next(); t = t + PTR; } // 指针，如果有多个* Mul，例如(int **)，t对应的是指针的指针
      if (tk == ')') next(); else { printf("%d: bad cast\n", line); exit(-1); }
      expr(Inc); // 递归expr，优先级需要大于等于Inc，也就是处理i++ i--
      ty = t;
    }
    else { // 直接作为普通的表达式处理
      expr(Assign);
      if (tk == ')') next(); else { printf("%d: close paren expected\n", line); exit(-1); }
    }
  }
  else if (tk == Mul) { // 指针 *ptr
    next(); expr(Inc); // 递归expr，优先级大于等于INC，只需要判断 ++ --
    if (ty > INT) ty = ty - PTR; else { printf("%d: bad dereference\n", line); exit(-1); } // 减一层PTR
    *++e = (ty == CHAR) ? LC : LI; // load，reg = *(reg)
  }
  else if (tk == And) { // &取地址
    next(); expr(Inc); // 递归expr，优先级大于等于INC，只需要判断 ++ --
    // 由于expr取变量时，默认是加上了LC/LI指令，这里需要取地址，则去掉这条load指令即可，对应的是
    if (*e == LC || *e == LI) --e; else { printf("%d: bad address-of\n", line); exit(-1); }
    ty = ty + PTR; // 再加一层PTR
  }
  else if (tk == '!') { next(); expr(Inc); *++e = PSH; *++e = IMM; *++e = 0; *++e = EQ; ty = INT; }
  else if (tk == '~') { next(); expr(Inc); *++e = PSH; *++e = IMM; *++e = -1; *++e = XOR; ty = INT; }
  else if (tk == Add) { next(); expr(Inc); ty = INT; }
  else if (tk == Sub) {
    next(); *++e = IMM;
    // 对于数字，直接加个符号，对于变量，使用MUL乘法运算乘上-1
    if (tk == Num) { *++e = -ival; next(); } else { *++e = -1; *++e = PSH; expr(Inc); *++e = MUL; }
    ty = INT;
  }
  else if (tk == Inc || tk == Dec) { // 自增和自减 ++i --i
    t = tk; next(); expr(Inc);
    // 可以参考下面 i++ i-- 的注释，这部分逻辑更简单，因为是先进行加减操作，再传给左值
    if (*e == LC) { *e = PSH; *++e = LC; }
    else if (*e == LI) { *e = PSH; *++e = LI; }
    else { printf("%d: bad lvalue in pre-increment\n", line); exit(-1); }
    *++e = PSH;
    *++e = IMM; *++e = (ty > PTR) ? sizeof(int) : sizeof(char);
    *++e = (t == Inc) ? ADD : SUB;
    *++e = (ty == CHAR) ? SC : SI;
  }
  else { printf("%d: bad expression\n", line); exit(-1); }

  while (tk >= lev) { // "precedence climbing" or "Top Down Operator Precedence" method
                      // 参考逆波兰式，例如 1+1 先把两个1入栈，运算符在栈顶，也就是 | 1 | 1 | + |
                      // 1+1*2则是 | 1 | 1 | 2 | * | + |，先运算乘法，然后运算加法
                      // 这种expr入栈的操作是递归的，使用优先级爬坡的方式设定终点，也就是终止于最高优先级
                      // 这里有一个while循环，是指在每一层expr递归时也是可以往前遍历的
                      // 例如1+1*2/2，除号和乘号优先级平行，不需要递归，是顺序遍历的，对应的栈如下
                      // | 1 | 1 | 2 | * | 2 | / | + |
    t = ty;
    if (tk == Assign) {
      next();
      // 修改LC/LI为PSH，把变量栈地址入栈，后面需要SC/SI store 变量
      if (*e == LC || *e == LI) *e = PSH; else { printf("%d: bad lvalue in assignment\n", line); exit(-1); }
      expr(Assign); *++e = ((ty = t) == CHAR) ? SC : SI;
    }
    else if (tk == Cond) { // 用jump来处理 a == 1 ? 0 : 1; 这样的运算，参考下图
      // |   BZ  |
      // |   b1  |
      // |   01  |
      // |  ...  |
      // |   0n  | 
      // |  JMP  |   
      // |   b2  |
      // |   11  | <-- b1
      // |  ...  |
      // |   1n  |
      // |       | <-- b2
      //
      // if else 和这个类似
      // 如果BZ判定成功，则顺序执行01-0n代码，到了JMP跳出else部分，否则，跳转到b1执行11-1n的else逻辑代码 
      next();
      *++e = BZ; d = ++e; // BZ 然后下一条指令直接跳过，先暂存在d中
      expr(Assign); // 冒号前的语句
      if (tk == ':') next(); else { printf("%d: conditional missing colon\n", line); exit(-1); }
      *d = (int)(e + 3); *++e = JMP; d = ++e; // BZ后面那个指令，存 e + 3 这个地址，也就是跳过了JMP xxx，到了Cond中
      expr(Cond); // 冒号后的语句
      *d = (int)(e + 1); // BZ/JMP后面那个指令，存 e + 1这个地址，也就是直接到了下一条指令
    }
    else if (tk == Lor) { next(); *++e = BNZ; d = ++e; expr(Lan); *d = (int)(e + 1); ty = INT; }
    else if (tk == Lan) { next(); *++e = BZ;  d = ++e; expr(Or);  *d = (int)(e + 1); ty = INT; }
    else if (tk == Or)  { next(); *++e = PSH; expr(Xor); *++e = OR;  ty = INT; }
    else if (tk == Xor) { next(); *++e = PSH; expr(And); *++e = XOR; ty = INT; }
    else if (tk == And) { next(); *++e = PSH; expr(Eq);  *++e = AND; ty = INT; }
    else if (tk == Eq)  { next(); *++e = PSH; expr(Lt);  *++e = EQ;  ty = INT; }
    else if (tk == Ne)  { next(); *++e = PSH; expr(Lt);  *++e = NE;  ty = INT; }
    else if (tk == Lt)  { next(); *++e = PSH; expr(Shl); *++e = LT;  ty = INT; }
    else if (tk == Gt)  { next(); *++e = PSH; expr(Shl); *++e = GT;  ty = INT; }
    else if (tk == Le)  { next(); *++e = PSH; expr(Shl); *++e = LE;  ty = INT; }
    else if (tk == Ge)  { next(); *++e = PSH; expr(Shl); *++e = GE;  ty = INT; }
    else if (tk == Shl) { next(); *++e = PSH; expr(Add); *++e = SHL; ty = INT; }
    else if (tk == Shr) { next(); *++e = PSH; expr(Add); *++e = SHR; ty = INT; }
    else if (tk == Add) {
      next(); *++e = PSH; expr(Mul); // 把加号后面的参数PUSH，然后expr递归，优先级要大于等于mul乘法
      if ((ty = t) > PTR) { 
        // 如果是指针，把前面计算的值PUSH进去，然后 IMM sizeof(int)，也就是指针长度，然后MUL，指针值单个长度乘以偏移
        // 这里type指向的是递归前数的值，所以就是把后面加上来的值乘以指针长度
        *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = MUL;
      }
      *++e = ADD; // 递归调用后再ADD，相当于把ADD指令放在了栈顶
                  // 如果对应于AST生成树，也就是先把ADD当做节点，先计算右子树，优先级越高离根越远
    }
    else if (tk == Sub) {
      next(); *++e = PSH; expr(Mul);
      // 指针减法分为两种，两个指针相减，得到的是索引的差，类似于数组的下标，如果是指针与数相减，得到的是指针的地址
      if (t > PTR && t == ty) {
        *++e = SUB; *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = DIV; ty = INT; // 两个指针地址相减
      }
      else if ((ty = t) > PTR) { 
        *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = MUL; *++e = SUB; // 这里和加法就是一样的了
      }
      else *++e = SUB;
    }
    else if (tk == Mul) { next(); *++e = PSH; expr(Inc); *++e = MUL; ty = INT; }
    else if (tk == Div) { next(); *++e = PSH; expr(Inc); *++e = DIV; ty = INT; }
    else if (tk == Mod) { next(); *++e = PSH; expr(Inc); *++e = MOD; ty = INT; }
    else if (tk == Inc || tk == Dec) {
      // i++ i--，处理和--i ++i 类似
      // 示例如下:
      // char *c;
      // char i;
      // c = malloc(1);
      // *c = 'a';
      // i = *c++;
      // 跟c相关的有三个地址，&c c的栈地址，c 分配的内存指针地址，*c 指针指向的值
      // PSH LC    ==> | &c  | reg = c 
      // PSH       ==> | &c  | c | reg = c    
      // IMM 1 ADD ==> | &c  | reg = c + 1 
      // SC        ==> |     | *(&c) = c + 1 reg = c + 1
      // PSH       ==> | c+1 | reg = c + 1
      // IMM 1 SUB ==> |     | reg = c
      // 最后的结果就是赋值给i的是c，但c的地址已经+1了
      // i = *c++ 的*星号取值部分在对应的expr进行处理
      if (*e == LC) { *e = PSH; *++e = LC; } // 由于需要对地址做偏移操作，所以改为PSH LC/LI
      else if (*e == LI) { *e = PSH; *++e = LI; }
      else { printf("%d: bad lvalue in post-increment\n", line); exit(-1); }
      *++e = PSH; *++e = IMM; *++e = (ty > PTR) ? sizeof(int) : sizeof(char);
      *++e = (tk == Inc) ? ADD : SUB; // ++ --
      *++e = (ty == CHAR) ? SC : SI;
      *++e = PSH; *++e = IMM; *++e = (ty > PTR) ? sizeof(int) : sizeof(char);
      *++e = (tk == Inc) ? SUB : ADD; // 赋给左值时，反过来操作一下
      next();
    }
    else if (tk == Brak) { //处理数组索引 例如 a[2] = 5;
      next(); *++e = PSH; expr(Assign); // 把第一个值PSH，然后expr递归计算
      if (tk == ']') next(); else { printf("%d: close bracket expected\n", line); exit(-1); }
      if (t > PTR) { *++e = PSH; *++e = IMM; *++e = sizeof(int); *++e = MUL;  } // 计算偏移
      else if (t < PTR) { printf("%d: pointer type expected\n", line); exit(-1); }
      *++e = ADD; // 得到偏移地址
      *++e = ((ty = t - PTR) == CHAR) ? LC : LI; // load 指针指向的值 
    }
    else { printf("%d: compiler error tk=%d\n", line, tk); exit(-1); }
  }
}

void stmt() // 不支持for循环
{
  int *a, *b;

  if (tk == If) {
    next();
    if (tk == '(') next(); else { printf("%d: open paren expected\n", line); exit(-1); }
    expr(Assign);
    if (tk == ')') next(); else { printf("%d: close paren expected\n", line); exit(-1); }
    *++e = BZ; b = ++e;
    stmt();
    if (tk == Else) {
      *b = (int)(e + 3); *++e = JMP; b = ++e;
      next();
      stmt();
    }
    *b = (int)(e + 1);
  }
  else if (tk == While) {
    next();
    a = e + 1;
    if (tk == '(') next(); else { printf("%d: open paren expected\n", line); exit(-1); }
    expr(Assign);
    if (tk == ')') next(); else { printf("%d: close paren expected\n", line); exit(-1); }
    *++e = BZ; b = ++e;
    stmt();
    *++e = JMP; *++e = (int)a;
    *b = (int)(e + 1);
  }
  else if (tk == Return) {
    next();
    if (tk != ';') expr(Assign);
    *++e = LEV;
    if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
  }
  else if (tk == '{') {
    next();
    while (tk != '}') stmt();
    next();
  }
  else if (tk == ';') {
    next();
  }
  else {
    expr(Assign);
    if (tk == ';') next(); else { printf("%d: semicolon expected\n", line); exit(-1); }
  }
}

int main(int argc, char **argv)
{
  int fd, bt, ty, poolsz, *idmain;
  int *pc, *sp, *bp, a, cycle; // vm registers
  int i, *t; // temps

  --argc; ++argv;
  if (argc > 0 && **argv == '-' && (*argv)[1] == 's') { src = 1; --argc; ++argv; }
  if (argc > 0 && **argv == '-' && (*argv)[1] == 'd') { debug = 1; --argc; ++argv; }
  if (argc < 1) { printf("usage: c4 [-s] [-d] file ...\n"); return -1; }

  if ((fd = open(*argv, 0)) < 0) { printf("could not open(%s)\n", *argv); return -1; }

  poolsz = 256*1024; // arbitrary size
  if (!(sym = malloc(poolsz))) { printf("could not malloc(%d) symbol area\n", poolsz); return -1; }
  if (!(le = e = malloc(poolsz))) { printf("could not malloc(%d) text area\n", poolsz); return -1; }
  if (!(data = malloc(poolsz))) { printf("could not malloc(%d) data area\n", poolsz); return -1; }
  if (!(sp = malloc(poolsz))) { printf("could not malloc(%d) stack area\n", poolsz); return -1; }

  memset(sym,  0, poolsz);
  memset(e,    0, poolsz);
  memset(data, 0, poolsz);

  // 先把这些特殊符号加到id table上，最后一个是main，程序从main开始运行
  p = "char else enum if int return sizeof while "
      "open read close printf malloc free memset memcmp exit void main";
  i = Char; while (i <= While) { next(); id[Tk] = i++; } // add keywords to symbol table
  i = OPEN; while (i <= EXIT) { next(); id[Class] = Sys; id[Type] = INT; id[Val] = i++; } // add library to symbol table
  next(); id[Tk] = Char; // handle void type
  next(); idmain = id; // keep track of main

  if (!(lp = p = malloc(poolsz))) { printf("could not malloc(%d) source area\n", poolsz); return -1; }
  if ((i = read(fd, p, poolsz-1)) <= 0) { printf("read() returned %d\n", i); return -1; }
  p[i] = 0;
  close(fd);

  // parse declarations
  line = 1;
  next();
  while (tk) {
    bt = INT; // basetype
    if (tk == Int) next(); // 处理 int 开头的定义
    else if (tk == Char) { next(); bt = CHAR; } // 处理 char 开头的定义 
    else if (tk == Enum) { // 处理 Enum 定义
      next();
      if (tk != '{') next();
      if (tk == '{') {
        next();
        i = 0;
        while (tk != '}') {
          if (tk != Id) { printf("%d: bad enum identifier %d\n", line, tk); return -1; }
          next();
          if (tk == Assign) {
            next();
            if (tk != Num) { printf("%d: bad enum initializer\n", line); return -1; }
            i = ival;
            next();
          }
          id[Class] = Num; id[Type] = INT; id[Val] = i++; // Enum，class作为Num处理
          if (tk == ',') next();
        }
        next();
      }
    }
    while (tk != ';' && tk != '}') {
      ty = bt;
      while (tk == Mul) { next(); ty = ty + PTR; } // 有*号，是指针，递归进行
      if (tk != Id) { printf("%d: bad global declaration\n", line); return -1; }
      if (id[Class]) { printf("%d: duplicate global definition\n", line); return -1; }
      next();
      id[Type] = ty;
      if (tk == '(') { // function 处理函数定义
        id[Class] = Fun;
        id[Val] = (int)(e + 1); // 函数地址
        next(); i = 0;
        while (tk != ')') { // 处理函数参数定义
          ty = INT;
          if (tk == Int) next();
          else if (tk == Char) { next(); ty = CHAR; }
          while (tk == Mul) { next(); ty = ty + PTR; }
          if (tk != Id) { printf("%d: bad parameter declaration\n", line); return -1; }
          if (id[Class] == Loc) { printf("%d: duplicate parameter definition\n", line); return -1; }
          // 如果之前定义过该变量（例如全局），暂存在HClass HType HVal中（如果没定义过则是空值），并设置新的Class Type Val
          id[HClass] = id[Class]; id[Class] = Loc;
          id[HType]  = id[Type];  id[Type] = ty;
          id[HVal]   = id[Val];   id[Val] = i++;
          next();
          if (tk == ',') next();
        }
        next();
        if (tk != '{') { printf("%d: bad function definition\n", line); return -1; }
        loc = ++i; // loc等于1
        next();
        while (tk == Int || tk == Char) { // 处理函数局部变量定义
          bt = (tk == Int) ? INT : CHAR;
          next();
          while (tk != ';') {
            ty = bt;
            while (tk == Mul) { next(); ty = ty + PTR; }
            if (tk != Id) { printf("%d: bad local declaration\n", line); return -1; }
            if (id[Class] == Loc) { printf("%d: duplicate local definition\n", line); return -1; }
            // 如果之前定义过该变量，暂存在HClass HType HVal中，并设置新的Class Type Val
            id[HClass] = id[Class]; id[Class] = Loc;
            id[HType]  = id[Type];  id[Type] = ty;
            id[HVal]   = id[Val];   id[Val] = ++i; // 局部变量从2开始计数
            next();
            if (tk == ',') next();
          }
          next();
        }
        *++e = ENT; *++e = i - loc; // enter 函数，ENT i - loc，也就是局部变量的个数
        while (tk != '}') stmt();
        *++e = LEV; // 退出函数
        id = sym; // unwind symbol table locals
        while (id[Tk]) {
          if (id[Class] == Loc) { // 恢复Loc变量Class Type Val
            id[Class] = id[HClass];
            id[Type] = id[HType];
            id[Val] = id[HVal];
          }
          id = id + Idsz;
        }
      }
      else { // 全局变量
        id[Class] = Glo;
        id[Val] = (int)data;
        data = data + sizeof(int);
      }
      if (tk == ',') next();
    }
    next();
  }

  if (!(pc = (int *)idmain[Val])) { printf("main() not defined\n"); return -1; } // pc指针指向idman[Val] 也就是main在汇编中的地址
  if (src) return 0;

  // setup stack
  bp = sp = (int *)((int)sp + poolsz); // bp是base pointer基地址
  *--sp = EXIT; // call exit if main returns
                // 栈底，自上往下，EXIT和PSH其实是opcode，相当于这两个指令直接存在栈底
                // 这里要注意一点，当pc程序指针走到PSH时，是继续往高地址走，而不是像栈往低地址走
                // 所以当退出main函数时，最后两条指令是PSH EXIT，PSH是把return的值入栈，然后EXIT取走这个值
  *--sp = PSH; t = sp; // PUSH
  *--sp = argc; // main第一个参数
  *--sp = (int)argv; // main第二个参数
  *--sp = (int)t; // 这里存的是t的地址，也就是指向栈底的PSH opcode位置 

  // 栈帧布局参考下图
  // 以进入 main 为例
  // LEA 3 指向的是 argc
  // LEA 2 指向的是 argv
  // LEA 1 上一层调用的 pc+1 地址
  // LEA 0 是指向上一层调用的栈帧基地址，然后当前BP指向这一层的栈帧基地址
  // SP 指向栈顶
  // LEA -1 是第一个局部变量
  // LEA 是基于 BP 做偏移，如果是正值，则是往上走，可以索引函数参数，如果是负值，往下走，可以索引局部变量
  // ENT 进入函数时，会带上这个函数的参数个数
  //
  // |  arg1  |  3
  // |  arg2  |  2
  // | pc + 1 |  1
  // | pre bp |  0
  // | local1 | -1
  // | local2 | -2
 
  // run...
  cycle = 0;
  while (1) {
    i = *pc++; ++cycle; // 每个指令就是一个PC，ENT 2是两个PC，分别是ENT和2
    if (debug) {
      printf("%d> %.4s", cycle,
        &"LEA ,IMM ,JMP ,JSR ,BZ  ,BNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PSH ,"
         "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
         "OPEN,READ,CLOS,PRTF,MALC,FREE,MSET,MCMP,EXIT,"[i * 5]);
      if (i <= ADJ) printf(" %d\n", *pc); else printf("\n");
    }
    if      (i == LEA) a = (int)(bp + *pc++);                             // load local address
    else if (i == IMM) a = *pc++;                                         // load global address or immediate
    else if (i == JMP) pc = (int *)*pc;                                   // jump
    // 把这一层的下一条pc指令存入sp中，pc更新为跳转的地址
    else if (i == JSR) { *--sp = (int)(pc + 1); pc = (int *)*pc; }        // jump to subroutine
    // 如果相等则顺序执行，如果不等则跳转到指向的地址
    else if (i == BZ)  pc = a ? pc + 1 : (int *)*pc;                      // branch if zero
    else if (i == BNZ) pc = a ? (int *)*pc : pc + 1;                      // branch if not zero
    // 存上一层bp栈帧基地址，然后bp更新为当前sp地址，sp再指向新的stack栈顶，偏移量直接跳过了局部变量个数
    else if (i == ENT) { *--sp = (int)bp; bp = sp; sp = sp - *pc++; }     // enter subroutine
    else if (i == ADJ) sp = sp + *pc++;                                   // stack adjust
    // 退出当前函数调用，bp取回上一层的栈帧基地址，pc取回之前存入的pc地址，sp往上移动两个位置
    else if (i == LEV) { sp = bp; bp = (int *)*sp++; pc = (int *)*sp++; } // leave subroutine
    else if (i == LI)  a = *(int *)a;                                     // load int
    else if (i == LC)  a = *(char *)a;                                    // load char
    // SI/SC从栈顶取变量赋值给地址指向的值
    else if (i == SI)  *(int *)*sp++ = a;                                 // store int
    else if (i == SC)  a = *(char *)*sp++ = a;                            // store char
    else if (i == PSH) *--sp = a;                                         // push

    // 以下运算，都是要从栈中取出一个值，然后跟 a 寄存器做相应的运算，再更新到 a
    else if (i == OR)  a = *sp++ |  a;
    else if (i == XOR) a = *sp++ ^  a;
    else if (i == AND) a = *sp++ &  a;
    else if (i == EQ)  a = *sp++ == a;
    else if (i == NE)  a = *sp++ != a;
    else if (i == LT)  a = *sp++ <  a;
    else if (i == GT)  a = *sp++ >  a;
    else if (i == LE)  a = *sp++ <= a;
    else if (i == GE)  a = *sp++ >= a;
    else if (i == SHL) a = *sp++ << a;
    else if (i == SHR) a = *sp++ >> a;
    else if (i == ADD) a = *sp++ +  a;
    else if (i == SUB) a = *sp++ -  a;
    else if (i == MUL) a = *sp++ *  a;
    else if (i == DIV) a = *sp++ /  a;
    else if (i == MOD) a = *sp++ %  a;

    // 以下都是系统调用，出栈在函数调用处做了处理
    else if (i == OPEN) a = open((char *)sp[1], *sp);
    else if (i == READ) a = read(sp[2], (char *)sp[1], *sp);
    else if (i == CLOS) a = close(*sp);
    // 这里printf最多处理6个参数，引用pc是方便取参数
    // 例如 printf("%d\n", a) 当前 pc 为 ADJ 2，pc[1] = 2，也就是参数个数
    // sp + 2 得到的是第一个参数往上的栈地址，然后以此为基准来按顺序取参数
    // 这里实际上引用了一些空参数，但由于printf自己做了处理所有没有关系
    else if (i == PRTF) { t = sp + pc[1]; a = printf((char *)t[-1], t[-2], t[-3], t[-4], t[-5], t[-6]); }
    else if (i == MALC) a = (int)malloc(*sp);
    else if (i == FREE) free((void *)*sp);
    else if (i == MSET) a = (int)memset((char *)sp[2], sp[1], *sp);
    else if (i == MCMP) a = memcmp((char *)sp[2], (char *)sp[1], *sp);
    else if (i == EXIT) { printf("exit(%d) cycle = %d\n", *sp, cycle); return *sp; }
    else { printf("unknown instruction = %d! cycle = %d\n", i, cycle); return -1; }
  }
}
