#include <string.h>
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <stdio.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/dma.h"   
#include "hardware/i2c.h"
#include "lwip/tcp.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include <stdlib.h>
#include "inc/ssd1306.h"
#include <ctype.h>
#include "pico/binary_info.h"
#include "pico/multicore.h"

void salvar_senha_na_flash(const char *nova);
void carregar_senha_da_flash(char *destino);

#define TCP_PORT 80
#define DEBUG_printf printf
#define POLL_TIME_S 5
#define HTTP_GET "GET"
#define HTTP_RESPONSE_HEADERS "HTTP/1.1 %d OK\nContent-Length: %d\nContent-Type: text/html; charset=utf-8\nConnection: close\n\n"
#define LED_TEST_BODY "<html> \
  <body> \
    <h1>Gerenciamento de Acesso</h1> \
    <button><a href=\"?desbloqueio=1\">Desbloquear Sistema</a></button> \
    <p>%s %s </p> \
    <h2>Alterar senha</h2> \
    <form action=\"/gerenciar_acesso\" method=\"get\"> \
        <label for=\"senha\">Nova senha: </label> \
        <input type=\"text\" placeholder=\"Digite a senha\" name=\"senha\"> \
        <input type=\"submit\" value=\"Submit\"> \
    </form> \
    <p>%s</p> \
    <p>Senha: %s</p> \
  </body> \
</html>"
#define LED_PARAM "desbloqueio=%d"
#define SENHA "senha=%s"
#define LED_TEST "/gerenciar_acesso"
#define RELE 1
#define HTTP_RESPONSE_REDIRECT "HTTP/1.1 302 Redirect\nLocation: http://%s" LED_TEST "\n\n"

#define TAMANHO_MAX_SENHA 8 // define o tamanho máximo da senha, seguro na memória RAM
#define DESLOCAMENTO_DO_ALVO_FLASH (PICO_FLASH_SIZE_BYTES - 4096) // Define a posição onde começará a gravar os dados da senha na memória flash 
#define DESLOCAMENTO_DA_SENHA_FLASH (PICO_FLASH_SIZE_BYTES - 4096) // serve como documentação no código, mas não é necessário para o funcionamento
#define TAMANHO_DA_SENHA_FLASH 6 //  Tamanho máximo da senha a ser gravada na memória flash. Aqui, 5 caracteres reservados para a senha e 1 para o terminador nulo '\0'
const uint8_t *conteudo_alvo_flash = (const uint8_t *)(XIP_BASE + DESLOCAMENTO_DA_SENHA_FLASH); // Ponteiro para leitura da flash

// Teclado 4x4
#define LINHAS 4
#define COLUNAS 4

const uint I2C_SDA = 14;
const uint I2C_SCL = 15;

int y = 0;  // Primeira linha (linha 0 → topo)
int x = 0;  // Começa na coluna 0
#define I2C_PORT i2c1
#define TAMANHO_SENHA 5

// Pinagem
const uint ROW_PINS[4] = {4, 8, 9, 16};      // Linhas como INPUT com PULL_UP
const uint COL_PINS[4] = {17, 18, 19, 20};   // Colunas como OUTPUT
char senha[6] = ""; // Senha de 6 dígitos
char senhaDigitada[6] = "";
// Keymap
const char keymap[4][4] = {
    {'D', 'C', 'B', 'A'},
    {'#', '9', '6', '3'},
    {'0', '8', '5', '2'},
    {'*', '7', '4', '1'}
};

int contador = 0;
int contadorDigitoSenha = 0;
int tentativas = 0;
char tecla;
int bloquearSistema = 0;

char novaSenha[6];
// uint8_t ssd[1024];  // tamanho já conhecido
// struct render_area frame_area;

void tarefa_nucleo1() {

    gpio_init(RELE);    
    gpio_set_dir(RELE, GPIO_OUT);
    gpio_put(RELE, 1); // Desliga o rele inicialmente
    // Processo de inicialização completo do OLED SSD1306
    ssd1306_init();
    uint8_t ssd[1024];  // tamanho já conhecido
    // Preparar área de renderização para o display (ssd1306_width pixels por ssd1306_n_pages páginas)
    struct render_area frame_area = {
        start_column : 0,
        end_column : ssd1306_width - 1,
        start_page : 0,
        end_page : ssd1306_n_pages - 1
    };

    calculate_render_area_buffer_length(&frame_area);
    static int bloqueadoAnterior = 0;  // Estado anterior do bloqueio

    // zera o display inteiro
    //uint8_t ssd[ssd1306_buffer_length];
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);

    ssd1306_draw_string(ssd, x, y, "Digite a senha:");
    render_on_display(ssd, &frame_area);

    while(true) {

        // Se acabou de ser desbloqueado via web
        if (bloquearSistema == 0 && bloqueadoAnterior == 1) {
            bloqueadoAnterior = 0;
            memset(ssd, 0, ssd1306_buffer_length);
            render_on_display(ssd, &frame_area);
            ssd1306_draw_string(ssd, 0, 16, "Desbloqueado!");
            render_on_display(ssd, &frame_area);
            sleep_ms(4000);
            memset(ssd, 0, ssd1306_buffer_length);
            render_on_display(ssd, &frame_area);
            x = 0;
            ssd1306_draw_string(ssd, 0, 0, "Digite a senha:");
           render_on_display(ssd, &frame_area);
        }

        // Se o sistema está bloqueado
        if (bloquearSistema == 1) {
             bloqueadoAnterior = 1;
            // memset(ssd, 0, ssd1306_buffer_length);
            // render_on_display(ssd, &frame_area);
            // ssd1306_draw_string(ssd, 0, 16, "Acesso bloqueado!");
            // render_on_display(ssd, &frame_area);
             sleep_ms(500);
            continue;
        }

        
    
        for (int c = 0; c < 4; c++) {
                // Coloca coluna atual em LOW
                gpio_put(COL_PINS[c], 0);

                sleep_ms(1); // Pequeno delay para estabilizar

                for (int r = 0; r < 4; r++) {
                    // Verifica se linha está LOW (tecla pressionada)
                        if (!gpio_get(ROW_PINS[r])) {
                            tecla = keymap[r][c];

                            if(contadorDigitoSenha >= 5) {
                                if(tecla == '#') {
                                    if(strcmp(senhaDigitada, senha) == 0) {
                                        // printf("acesso liberado\n");
                                        ssd1306_draw_string(ssd, 0, 32, "Acesso liberado!");                    
                                        render_on_display(ssd, &frame_area);
                                        gpio_put(RELE, 0); // Aciona fechadura através do rele
                                        sleep_ms(4000);
                                        gpio_put(RELE, 1); // Desliga fechadura através do rele
                                        memset(ssd, 0, ssd1306_buffer_length);
                                        render_on_display(ssd, &frame_area);
                                        contadorDigitoSenha = 0;
                                        senhaDigitada[0] = '\0';
                                        //contador = 0;
                                        tentativas = 0;
                                        x = 0;
                                        ssd1306_draw_string(ssd, 0, 0, "Digite a senha");                    
                                        render_on_display(ssd, &frame_area);

                                    } else {
                                        tentativas += 1;

                                        if(tentativas == 3) {
                                            // printf("Acesso bloqueado\n");
                                            memset(ssd, 0, ssd1306_buffer_length);
                                            render_on_display(ssd, &frame_area);
                                            ssd1306_draw_string(ssd, 0, 16, "Acesso bloqueado!\n");                    
                                            render_on_display(ssd, &frame_area);
                                            bloquearSistema = 1;  
                                            contadorDigitoSenha = 0;                                  
                                        }

                                        if(tentativas < 3 && tentativas >= 0) {
                                            ssd1306_draw_string(ssd, 0, 32, "Senha errada!");                    
                                            render_on_display(ssd, &frame_area);
                                            sleep_ms(3000);
                                            memset(ssd, 0, ssd1306_buffer_length);
                                            render_on_display(ssd, &frame_area);
                                            senhaDigitada[0] = '\0';
                                            contadorDigitoSenha = 0;
                                            x = 0;
                                            ssd1306_draw_string(ssd, 0, 0, "Digite a senha");                    
                                            render_on_display(ssd, &frame_area);
                                        }
                                        
                                    }       
                                } 
                                
                            }   else if(tecla == '*') {
                                    // Limpa a senha digitada
                                    // printf("Limpando senha digitada\n");
                                    ssd1306_draw_string(ssd, 0, 32, "Senha apagada!");                    
                                    render_on_display(ssd, &frame_area);
                                    sleep_ms(3000);
                                    memset(ssd, 0, ssd1306_buffer_length);
                                    render_on_display(ssd, &frame_area);
                                    senhaDigitada[0] = '\0';
                                    contadorDigitoSenha = 0;
                                    x = 0;
                                    ssd1306_draw_string(ssd, 0, 0, "Digite a senha");                    
                                    render_on_display(ssd, &frame_area);
                                }   else {
                                        //contador += 1;
                                        //strncat(senhaDigitada, tecla);
                                        if (contadorDigitoSenha < TAMANHO_SENHA && tecla != '#') {
                                            senhaDigitada[contadorDigitoSenha] = tecla;
                                            senhaDigitada[contadorDigitoSenha + 1] = '\0';  // garante final da string
                                            contadorDigitoSenha++;
                                            // printf("Tecla digitada: %c\n", tecla);
                                        }
                                        // <<< saíde >>> 
                                        //printf("Tecla pressionada: %c\n", tecla);

                                        ssd1306_draw_string(ssd, x, 16, "x");
                                        render_on_display(ssd, &frame_area);
                                        x += 8; // Avança para a próxima coluna
                                        // Debounce - espera soltar a tecla
                                        while(!gpio_get(ROW_PINS[r])) {
                                            sleep_ms(20);
                                        }
                                        sleep_ms(50); // Delay adicional após soltar
                                    }                        
                        }
                        
                }
                // Volta coluna para HIGH
                gpio_put(COL_PINS[c], 1);
        }
            sleep_ms(10);
    }
}

bool contem_caracteres_validos(char *senha) {
    const char *validos = "0123456789ABCDabcd";
    for (int i = 0; novaSenha[i] != '\0'; i++) {
        if (strchr(validos, senha[i])==NULL) {
            return false; // Encontrou caractere inválido
        }
    }
    return true; // todos os caracteres são válidos
}

typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    bool complete;
    ip_addr_t gw;
} TCP_SERVER_T;

typedef struct TCP_CONNECT_STATE_T_ {
    struct tcp_pcb *pcb;
    int sent_len;
    char headers[1024];
    char result[1024];
    int header_len;
    int result_len;
    ip_addr_t *gw;
} TCP_CONNECT_STATE_T;

static err_t tcp_close_client_connection(TCP_CONNECT_STATE_T *con_state, struct tcp_pcb *client_pcb, err_t close_err) {
    if (client_pcb) {
        assert(con_state && con_state->pcb == client_pcb);
        tcp_arg(client_pcb, NULL);
        tcp_poll(client_pcb, NULL, 0);
        tcp_sent(client_pcb, NULL);
        tcp_recv(client_pcb, NULL);
        tcp_err(client_pcb, NULL);
        err_t err = tcp_close(client_pcb);
        if (err != ERR_OK) {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(client_pcb);
            close_err = ERR_ABRT;
        }
        if (con_state) {
            free(con_state);
        }
    }
    return close_err;
}

static void tcp_server_close(TCP_SERVER_T *state) {
    if (state->server_pcb) {
        tcp_arg(state->server_pcb, NULL);
        tcp_close(state->server_pcb);
        state->server_pcb = NULL;
    }
}

static err_t tcp_server_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    DEBUG_printf("tcp_server_sent %u\n", len);
    con_state->sent_len += len;
    if (con_state->sent_len >= con_state->header_len + con_state->result_len) {
        DEBUG_printf("all done\n");
        return tcp_close_client_connection(con_state, pcb, ERR_OK);
    }
    return ERR_OK;
}

static int test_server_content(const char *request, const char *params, char *result, size_t max_result_len) {
    int len = 0;
    if (strncmp(request, LED_TEST, sizeof(LED_TEST) - 1) == 0) {
        int led_state = 0;
        char mensagem_desbloqueio[64] = "";

        // Verifica se houve requisição de desbloqueio
        if (params) {
            int desbloqueio_req = sscanf(params, LED_PARAM, &led_state);
            
            if (desbloqueio_req == 1 && led_state == 1) {
                bloquearSistema = 0;
                tentativas = 0; // Reinicia tentativas
                strcpy(mensagem_desbloqueio, "Sistema desbloqueado com sucesso!");
            }
        }

        // Continua renderizando a página normalmente com as informações
        len = snprintf(result, max_result_len, LED_TEST_BODY,
            (bloquearSistema ? "Sistema Bloqueado" : "Sistema Liberado"),"",mensagem_desbloqueio, "");

        // Se também houver alteração de senha na mesma URL
        int senhaWeb = sscanf(params, SENHA, &novaSenha);
        if (senhaWeb == 1) {                
            if ((strchr(novaSenha, '*') != NULL) || (strstr(novaSenha, "%23") != NULL)) {
                len = snprintf(result, max_result_len, LED_TEST_BODY, (bloquearSistema ? "Sistema Bloqueado" : "Sistema Liberado"), "", "Senha não permitida! A senha não pode conter * ou #.", "");
            } else if (strlen(novaSenha) != 5) {
                len = snprintf(result, max_result_len, LED_TEST_BODY, (bloquearSistema ? "Sistema Bloqueado" : "Sistema Liberado"), "", "Senha não permitida! A senha deve conter 5 dígitos.", novaSenha);
            } else if (!contem_caracteres_validos(novaSenha)) {
                len = snprintf(result, max_result_len, LED_TEST_BODY, (bloquearSistema ? "Sistema Bloqueado" : "Sistema Liberado"), "", "Você digitou uma letra não permitida!", "");
            } else {
                strcpy(senha, novaSenha);
                salvar_senha_na_flash(novaSenha);
                strcpy(senha, novaSenha);
                len = snprintf(result, max_result_len, LED_TEST_BODY, (bloquearSistema ? "Sistema Bloqueado" : "Sistema Liberado"), "", "Senha alterada com sucesso!", novaSenha);
            }
        }
    }
    return len;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    if (!p) {
        DEBUG_printf("connection closed\n");
        return tcp_close_client_connection(con_state, pcb, ERR_OK);
    }
    assert(con_state && con_state->pcb == pcb);
    if (p->tot_len > 0) {
        DEBUG_printf("tcp_server_recv %d err %d\n", p->tot_len, err);
#if 0
        for (struct pbuf *q = p; q != NULL; q = q->next) {
            DEBUG_printf("in: %.*s\n", q->len, q->payload);
        }
#endif
        // Copy the request into the buffer
        pbuf_copy_partial(p, con_state->headers, p->tot_len > sizeof(con_state->headers) - 1 ? sizeof(con_state->headers) - 1 : p->tot_len, 0);

        // Handle GET request
        if (strncmp(HTTP_GET, con_state->headers, sizeof(HTTP_GET) - 1) == 0) {
            char *request = con_state->headers + sizeof(HTTP_GET); // + space
            char *params = strchr(request, '?');
            if (params) {
                if (*params) {
                    char *space = strchr(request, ' ');
                    *params++ = 0;
                    if (space) {
                        *space = 0;
                    }
                } else {
                    params = NULL;
                }
            }

            // Generate content
            con_state->result_len = test_server_content(request, params, con_state->result, sizeof(con_state->result));
            DEBUG_printf("Request: %s?%s\n", request, params);
            DEBUG_printf("Result: %d\n", con_state->result_len);

            // Check we had enough buffer space
            if (con_state->result_len > sizeof(con_state->result) - 1) {
                DEBUG_printf("Too much result data %d\n", con_state->result_len);
                return tcp_close_client_connection(con_state, pcb, ERR_CLSD);
            }

            // Generate web page
            if (con_state->result_len > 0) {
                con_state->header_len = snprintf(con_state->headers, sizeof(con_state->headers), HTTP_RESPONSE_HEADERS,
                    200, con_state->result_len);
                if (con_state->header_len > sizeof(con_state->headers) - 1) {
                    DEBUG_printf("Too much header data %d\n", con_state->header_len);
                    return tcp_close_client_connection(con_state, pcb, ERR_CLSD);
                }
            } else {
                // Send redirect
                con_state->header_len = snprintf(con_state->headers, sizeof(con_state->headers), HTTP_RESPONSE_REDIRECT,
                    ipaddr_ntoa(con_state->gw));
                DEBUG_printf("Sending redirect %s", con_state->headers);
            }

            // Send the headers to the client
            con_state->sent_len = 0;
            err_t err = tcp_write(pcb, con_state->headers, con_state->header_len, 0);
            if (err != ERR_OK) {
                DEBUG_printf("failed to write header data %d\n", err);
                return tcp_close_client_connection(con_state, pcb, err);
            }

            // Send the body to the client
            if (con_state->result_len) {
                err = tcp_write(pcb, con_state->result, con_state->result_len, 0);
                if (err != ERR_OK) {
                    DEBUG_printf("failed to write result data %d\n", err);
                    return tcp_close_client_connection(con_state, pcb, err);
                }
            }
        }
        tcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_server_poll(void *arg, struct tcp_pcb *pcb) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    DEBUG_printf("tcp_server_poll_fn\n");
    return tcp_close_client_connection(con_state, pcb, ERR_OK); // Just disconnect clent?
}

static void tcp_server_err(void *arg, err_t err) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err_fn %d\n", err);
        tcp_close_client_connection(con_state, con_state->pcb, err);
    }
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if (err != ERR_OK || client_pcb == NULL) {
        DEBUG_printf("failure in accept\n");
        return ERR_VAL;
    }
    DEBUG_printf("client connected\n");

    // Create the state for the connection
    TCP_CONNECT_STATE_T *con_state = calloc(1, sizeof(TCP_CONNECT_STATE_T));
    if (!con_state) {
        DEBUG_printf("failed to allocate connect state\n");
        return ERR_MEM;
    }
    con_state->pcb = client_pcb; // for checking
    con_state->gw = &state->gw;

    // setup connection to client
    tcp_arg(client_pcb, con_state);
    tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_poll(client_pcb, tcp_server_poll, POLL_TIME_S * 2);
    tcp_err(client_pcb, tcp_server_err);

    return ERR_OK;
}

static bool tcp_server_open(void *arg, const char *ap_name) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    DEBUG_printf("starting server on port %d\n", TCP_PORT);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        DEBUG_printf("failed to create pcb\n");
        return false;
    }

    err_t err = tcp_bind(pcb, IP_ANY_TYPE, TCP_PORT);
    if (err) {
        DEBUG_printf("failed to bind to port %d\n",TCP_PORT);
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        DEBUG_printf("failed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    printf("Try connecting to '%s' (press 'd' to disable access point)\n", ap_name);
    return true;
}

void key_pressed_func(void *param) {
    assert(param);
    TCP_SERVER_T *state = (TCP_SERVER_T*)param;
    int key = getchar_timeout_us(0); // get any pending key press but don't wait
    if (key == 'd' || key == 'D') {
        cyw43_arch_lwip_begin();
        cyw43_arch_disable_ap_mode();
        cyw43_arch_lwip_end();
        state->complete = true;
    }
}

void salvar_senha_na_flash(const char *nova) {
    uint8_t buffer[FLASH_PAGE_SIZE];
    memcpy(buffer, conteudo_alvo_flash, FLASH_PAGE_SIZE); // Lê o setor atual
    memcpy(buffer, nova, TAMANHO_DA_SENHA_FLASH); // Copia nova senha nos primeiros bytes

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(DESLOCAMENTO_DA_SENHA_FLASH, FLASH_SECTOR_SIZE); // Apaga setor
    flash_range_program(DESLOCAMENTO_DA_SENHA_FLASH, buffer, FLASH_PAGE_SIZE); // Grava
    restore_interrupts(ints);
}

void carregar_senha_da_flash(char *destino) {
    memcpy(destino, conteudo_alvo_flash, TAMANHO_DA_SENHA_FLASH - 1);
    destino[TAMANHO_DA_SENHA_FLASH - 1] = '\0'; // Garante string válida
}

int main() {
    stdio_init_all();
    // salvar_senha_na_flash("12345");
    carregar_senha_da_flash(senha);

    // Inicialização do i2c
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Configura linhas como INPUT com PULL_UP
    for (int i = 0; i < 4; i++) {
        gpio_init(ROW_PINS[i]);
        gpio_set_dir(ROW_PINS[i], GPIO_IN);
        gpio_pull_up(ROW_PINS[i]);
    }

    // Configura colunas como OUTPUT
    for (int i = 0; i < 4; i++) {
        gpio_init(COL_PINS[i]);
        gpio_set_dir(COL_PINS[i], GPIO_OUT);
        gpio_put(COL_PINS[i], 1); // Inicialmente HIGH
    } 
    
    multicore_launch_core1(tarefa_nucleo1);
    TCP_SERVER_T *state = calloc(1, sizeof(TCP_SERVER_T));
    if (!state) {
        DEBUG_printf("failed to allocate state\n");
        return 1;
    }

    if (cyw43_arch_init()) {
        DEBUG_printf("failed to initialise\n");
        return 1;
    }

    // Get notified if the user presses a key
    stdio_set_chars_available_callback(key_pressed_func, state);

    const char *ap_name = "picow_test";
#if 1
    const char *password = "password";
#else
    const char *password = NULL;
#endif

    cyw43_arch_enable_ap_mode(ap_name, password, CYW43_AUTH_WPA2_AES_PSK);

    #if LWIP_IPV6
    #define IP(x) ((x).u_addr.ip4)
    #else
    #define IP(x) (x)
    #endif

    ip4_addr_t mask;
    IP(state->gw).addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    IP(mask).addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);

    #undef IP

    // Start the dhcp server
    dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &state->gw, &mask);

    // Start the dns server
    dns_server_t dns_server;
    dns_server_init(&dns_server, &state->gw);

    if (!tcp_server_open(state, ap_name)) {
        DEBUG_printf("failed to open server\n");
        return 1;
    }

    state->complete = false;
    while(!state->complete) {       
        // the following #ifdef is only here so this same example can be used in multiple modes;
        // you do not need it in your code
        
#if PICO_CYW43_ARCH_POLL
        // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
        // main loop (not from a timer interrupt) to check for Wi-Fi driver or lwIP work that needs to be done.
        cyw43_arch_poll();
        // you can poll as often as you like, however if you have nothing else to do you can
        // choose to sleep until either a specified time, or cyw43_arch_poll() has work to do:
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1000));
#else
        // if you are not using pico_cyw43_arch_poll, then Wi-FI driver and lwIP work
        // is done via interrupt in the background. This sleep is just an example of some (blocking)
        // work you might be doing.
        sleep_ms(1000);
#endif
    if(!bloquearSistema) {
        
        }
    }
    tcp_server_close(state);
    dns_server_deinit(&dns_server);
    dhcp_server_deinit(&dhcp_server);
    cyw43_arch_deinit();
    printf("Test complete\n");
    return 0;
}
