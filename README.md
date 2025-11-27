
---
# 🔐 Sistema de Controle de Acesso com Raspberry Pi Pico W    
**Teclado Matricial + OLED SSD1306 + Wi-Fi AP + Servidor HTTP**

Este projeto implementa um sistema completo e autônomo de **controle de acesso** usando a **Raspberry Pi Pico W**. Ele combina diversos módulos de hardware e software para criar uma solução robusta e funcional, incluindo:

- 🧮 Teclado matricial 4×4  
- 🖥️ Display OLED SSD1306 via I2C  
- 🌐 Ponto de acesso Wi-Fi (AP Mode)  
- 🕸️ Servidor HTTP para gerenciamento e alteração de senha  
- 🔒 Bloqueio automático após tentativas erradas  
- 💾 Armazenamento da senha na memória flash  
- 🔁 Execução em multicore (Core 0 = rede / Core 1 = controle)

---

## 📌 Funcionalidades

### 🔢 1. Digitação da senha via teclado 4×4
- Aceita senha de 5 dígitos.  
- Cada tecla digitada aparece como `x` no display OLED.  
- Tecla `#` → valida senha  
- Tecla `*` → apaga senha digitada  

### 🖨️ 2. Feedback no display OLED SSD1306
Exibe mensagens como:
- “Digite a senha”  
- “Acesso liberado!”  
- “Senha errada!”  
- “Acesso bloqueado!”  
- “Senha apagada!”  

### 🔒 3. Sistema de bloqueio
- Após **3 tentativas incorretas**, o sistema é bloqueado.  
- O desbloqueio só pode ser feito pela página web.

### 🌐 4. Servidor HTTP + Ponto de Acesso Wi-Fi
A Pico W cria um AP:

```

SSID: picow_test
Senha: password

```

A página é acessível via navegador:

```

[http://192.168.4.1/gerenciar_acesso](http://192.168.4.1/gerenciar_acesso)

```

Funções disponíveis:
- ✔ Desbloquear sistema  
- ✔ Alterar senha  
- ✔ Visualizar status

A página usa parâmetros GET como:

```

/gerenciar_acesso?senha=12345
/gerenciar_acesso?opcao=desbloquear

```

### 💾 5. Armazenamento persistente da senha (Flash)
- A senha é gravada no último setor da flash.  
- Mantém os dados mesmo após desligar.

---

## 🧩 Hardware Utilizado

| Componente | Função |
|-----------|--------|
| Raspberry Pi Pico W | Processamento + Wi-Fi |
| Teclado Matricial 4×4 | Entrada do usuário |
| Display OLED SSD1306 | Feedback visual |
| Relé (opcional) | Simulação de fechadura |
| Jumpers / Protoboard | Conexões |

### 📌 Mapeamento dos Pinos

#### Teclado 4×4
| Linhas | GPIO |
|--------|------|
| L1 | 4 |
| L2 | 8 |
| L3 | 9 |
| L4 | 16 |

| Colunas | GPIO |
|---------|------|
| C1 | 17 |
| C2 | 18 |
| C3 | 19 |
| C4 | 20 |

#### Display SSD1306 (I2C)
| Função | GPIO |
|--------|-------|
| SDA | 14 |
| SCL | 15 |

#### Relé
- GPIO 1

---

## 🏗️ Arquitetura do Software

### 🧠 Core 0 (Rede)
- Inicia ponto de acesso  
- Configura servidor HTTP  
- Responde requisições GET  
- Valida nova senha  
- Desbloqueia o sistema  
- Salva senha na flash  

### 🔧 Core 1 (Controle de Acesso)
- Lê o teclado matricial  
- Gerencia tentativas  
- Exibe estados no display  
- Aciona relé (fechadura)  
- Implementa lógica de tempo e reset  

---

## 🌍 Acesso à Página Web

Após conectar ao AP:

```

SSID: picow_test
Senha: password

```

Acesse:

```

[http://192.168.4.1/gerenciar_acesso](http://192.168.4.1/gerenciar_acesso)

````

### Funções da página:
- **Alterar senha**  
- **Desbloquear sistema**  
- **Visualizar status do LED/relé** (simulação de fechadura)

---

## 💾 Armazenamento da Senha na Flash

A senha é gravada usando:

```c
flash_range_erase(addr, 4096);
flash_range_program(addr, buffer, tamanho);
````

Carregada na inicialização via:

```c
memcpy(senha_atual, endereco_flash, 5);
```

O setor usado é:

```
PICO_FLASH_SIZE_BYTES - 4096
```

---

## ▶️ Compilação do Projeto

### Requisitos

* Pico SDK
* CMake
* ARM GCC Toolchain
* Biblioteca SSD1306 compatível

### Comandos

```bash
mkdir build
cd build
cmake ..
make
```

Gerar `.uf2` e copiar para a Pico segurando BOOTSEL.

---

