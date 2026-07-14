# 后端算法说明

> 纯 C++ 手工实现，零外部依赖 | 分支: `feat/encrypt`

本文档详细说明项目中压缩、加密、密钥派生三个核心后端算法的设计与实现。

---

## 1. 压缩算法：PackBits 风格 RLE

### 概述

Run-Length Encoding (RLE) 是一种简单的无损压缩算法，将连续重复的字节编码为「计数 + 值」对。本项目采用 PackBits 风格的 RLE，最早由 Apple 在 MacPaint 中使用，后用于 TIFF 图像格式。

### 编码格式

每个数据块以 1 字节控制头开始：

| 控制头 | 含义 | 数据 |
|---|---|---|
| `0x00–0x7F` (0–127) | 字面量块：接下来的 `N+1` 字节直接拷贝 | `N+1` 字节原始数据 |
| `0x80–0xFF` (128–255) | 行程块：将下一个字节重复 `N+1` 次 | 1 字节（重复值） |

其中 `N` = 控制头低 7 位。

### 编码策略

```
遍历输入数据:
  ├── 找到连续重复 ≥3 次的字节 → 编码为行程块（节省 ≥1 字节）
  └── 否则                     → 收集为字面量块，直到遇到可编码的行程或达到 128 字节上限
```

### 性能特征

| 数据类型 | 效果 |
|---|---|
| 大量重复字节（如 0x00 填充区） | 极高压缩率，8192 字节→约 128 字节 |
| 正常文本/代码 | 略有压缩或持平 |
| 随机/加密数据 | 可能略微膨胀（每 128 字面量多 1 控制字节，最坏 +0.8%） |

### 代码位置

- 头文件: `include/pipeline/StreamCompressor.h`
- 实现: `src/pipeline/StreamCompressor.cpp`

---

## 2. 加密算法：RC4 兼容流密码

### 概述

RC4 (Rivest Cipher 4) 是 Ron Rivest 于 1987 年设计的流密码。它结构简单（约 30 行核心代码），适合嵌入式和教育场景。本实现包含 RC4-drop256 增强（丢弃前 256 字节密钥流）。

### 算法流程

#### 第一阶段：密钥调度算法（KSA）

```
S[0..255] ← [0, 1, 2, ..., 255]
j ← 0
for i = 0 to 255:
    j ← (j + S[i] + K[i mod keyLen]) mod 256
    swap(S[i], S[j])
```

输入为「32 字节密钥 + 16 字节 IV」拼接后的 48 字节密钥材料。

#### 第二阶段：伪随机生成算法（PRGA）

```
i ← 0, j ← 0
for each output byte:
    i ← (i + 1) mod 256
    j ← (j + S[i]) mod 256
    swap(S[i], S[j])
    keystream ← S[(S[i] + S[j]) mod 256]
```

每次加密/解密调用产出与输入等长的密钥流，与数据 XOR。

#### RC4-drop256

初始化后立即运行 256 次 PRGA 并丢弃输出，以消除 RC4 密钥流初期偏差。

### 流式处理

S-box 状态跨 `encrypt()`/`decrypt()` 调用保持，支持分块流式处理。同一实例对同一数据流需维持 `init` → `encrypt` 序列（写入时）或 `init` → `decrypt` 序列（读取时）。

### 安全说明

RC4 已不适合高安全场景（存在密钥流偏差等已知弱点）。本项目中它作为轻量级混淆方案使用，配合密码保护和二进制归档格式提供基本的数据机密性。

### 代码位置

- 头文件: `include/pipeline/StreamEncryptor.h`
- 实现: `src/pipeline/StreamEncryptor.cpp`

---

## 3. 密钥派生：迭代混合函数

### 概述

自研的轻量级密钥派生函数（KDF），通过基于 32 位算术运算的类海绵结构将密码 + 盐值拉伸为指定长度的密钥材料。

### 常量

| 常量 | 值 |
|---|---|
| 内部状态大小 | 64 字节（16 个 uint32_t） |
| 混合轮数（每次迭代） | 8 轮 |
| 默认迭代次数 | 100,000 |

### 算法流程

```
deriveKey(password, salt, keySize, iterations):
    key ← []
    counter ← 0
    while len(key) < keySize:
        state[64] ← password ⊕ salt ⊕ counter
        for i = 1 to iterations:
            state ← mixState(state)
        key ← key + takeMin(keySize - len(key), 64) bytes from state
        counter ← counter + 1
    return key
```

#### mixState 函数

将 64 字节状态视为 16 个 uint32_t 字，执行 8 轮邻位混合：

```
for round = 0 to 7:
    for i = 0 to 15:
        s[i] ^= rotl(s[(i+1)%16], 7)
        s[i] += s[(i-1)%16]
        s[i] ^= rotl(s[i], 13)
        s[i] += s[(i+1)%16] ^ 0x9E3779B9  // 黄金比例常数
```

该结构借鉴了 FNV-1a 哈希和海绵函数的思想，通过简单的加、异或、旋转操作实现雪崩效应。

### 安全说明

此 KDF 为自研轻量级实现，迭代次数提供对抗暴力破解的工作因子。不适用于需要标准合规（如 FIPS-140）的场景。

### 随机数生成

`generateRandomBytes()` 从 `/dev/urandom` 读取密码学安全随机数，用于生成盐值和 IV。

### 安全清零

`secureClear()` 使用 `volatile` 指针强制编译器保留清零操作，防止敏感数据（密钥、密码）在内存中被优化残留。

### 代码位置

- 头文件: `include/pipeline/KeyDerivation.h`
- 实现: `src/pipeline/KeyDerivation.cpp`

---

## 4. 管道处理流程

```
                     ┌──────────────┐
   文件数据 (4KB)  → │  RLE 压缩     │  (可选, --compress)
                     └──────┬───────┘
                            ↓
                     ┌──────────────┐
                     │  RC4 加密     │  (可选, --encrypt)
                     └──────┬───────┘
                            ↓
                     ┌──────────────┐
                     │  写入归档文件  │
                     └──────────────┘
```

压缩在加密之前执行——明文数据压缩效率远高于密文。

---

## 变更历史

| 日期 | 变更 |
|---|---|
| 2026-07-14 | 初版：替代 zlib/OpenSSL，实现 RLE + RC4 + 迭代 KDF |
