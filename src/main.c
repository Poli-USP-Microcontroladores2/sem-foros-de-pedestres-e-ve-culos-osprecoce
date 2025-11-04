/* [INÍCIO - CÓDIGO SEMÁFORO DE PEDESTRES MODIFICADO] */
/*
 * Semáforo de Pedestres Sincronizado com Semáforo de Veículos
 *
 * Mapeamento de Pinos:
 * - led0: LED Verde (Pedestre)
 * - led1: LED Vermelho (Pedestre)
 * - PTA1: Botão de Pedestre (Local)
 *
 * NOVOS Pinos de Sincronização:
 * - PTB1: SYNC_OUT (Envia "REQUEST" para Veículos)
 * - PTB2: SYNC_IN (Recebe "SAFE" dos Veículos)
 *
 * Modo Normal (Sincronizado):
 * - Padrão: Vermelho sólido (STATE_RED_SOLID)
 * - Botão Pressionado (PTA1):
 * 1. Envia pulso "REQUEST" (via PTB1) para Veículos.
 * 2. Entra em STATE_WAITING_FOR_SAFE.
 * 3. Aguarda pulso "SAFE" (em PTB2) dos Veículos.
 * 4. Recebe "SAFE":
 * 5. Entra em STATE_GREEN_CYCLE: Verde (4s) -> Vermelho (2s)
 * 6. Volta ao Padrão (STATE_RED_SOLID)
 *
 * Modo Noturno:
 * - Vermelho: Pisca (1s ligado, 1s desligado)
 * - Botão e Sincronização: Ignorados
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_pedestre_sync, LOG_LEVEL_DBG);

/* 1. Definição dos LEDs (Usando DT) */
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);


/* 2. Definição Manual do Botão (PTA1) */
const struct device *gpioa_dev = DEVICE_DT_GET(DT_NODELABEL(gpioa));
#define BUTTON_PIN 1
static struct gpio_callback button_cb_data;


/* NOVO: 3. Definição dos Pinos de Sincronização (PTB1/PTB2) */
const struct device *gpiob_dev = DEVICE_DT_GET(DT_NODELABEL(gpiob));
#define SYNC_OUT_PIN     1 // PTB1
#define SYNC_IN_PIN      2 // PTB2
static struct gpio_callback sync_in_cb_data; // Callback para SYNC_IN


/* 4. Semáforos de Sinalização */
// ISR do Botão (PTA1) -> Thread Main
K_SEM_DEFINE(button_sem, 0, 1); 

// NOVO: ISR de Sync_IN (PTB2) -> Thread Main
K_SEM_DEFINE(safe_to_cross_sem, 0, 1); // Sinaliza "seguro" vindo do veículo


/* 5. Flag de Controle do Modo Noturno */
static volatile bool g_night_mode = false;

/* 6. Estados da Máquina */
enum state {
    STATE_NIGHT_BLINK,      // Modo Noturno: Piscando vermelho
    STATE_RED_SOLID,        // Modo Normal: Vermelho sólido (esperando botão)
    STATE_WAITING_FOR_SAFE, // NOVO: Esperando sinal "SAFE" (via SYNC_IN)
    STATE_GREEN_CYCLE       // Modo Normal: Ciclo do verde (4s) + vermelho (2s)
};

/*
 * 7. Callback (ISR) do Botão (PTA1)
 */
void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // Só sinaliza se NÃO estiver no modo noturno
    if (!g_night_mode) {
        // Libera o semáforo para a thread main
        k_sem_give(&button_sem);
    }
}

/*
 * NOVO: 8. Callback (ISR) do SYNC_IN (PTB2)
 *
 * Chamada quando a placa de VEÍCULOS envia o sinal "SAFE".
 */
void sync_in_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    if (pins & BIT(SYNC_IN_PIN)) {
        // Sinal "SEGURO PARA ATRAVESSAR" (PTB2) recebido
        // Só libera o semáforo se não estivermos em modo noturno
        if (!g_night_mode) {
            k_sem_give(&safe_to_cross_sem);
        }
    }
}


/*
 * 9. Função Main
 */
int main(void)
{
    int ret;
    enum state current_state;

    /* 9.1. Configuração dos LEDs (via DT) */
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        LOG_ERR("Dispositivo de LED não está pronto.");
        return 0;
    }
    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;
    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;

    /* 9.2. Configuração do Botão (PTA1 - Manual) */
    if (!device_is_ready(gpioa_dev)) {
        LOG_ERR("Dispositivo GPIOA (PTA1) não está pronto.");
        return 0;
    }
    // Configura o pino PTA1 como entrada com pull-up interno.
    ret = gpio_pin_configure(gpioa_dev, BUTTON_PIN, (GPIO_INPUT | GPIO_PULL_UP));
    if (ret < 0) {
        LOG_ERR("Falha ao configurar botão PTA1.");
        return 0;
    }
    // Configura a interrupção para borda de descida
    ret = gpio_pin_interrupt_configure(gpioa_dev, BUTTON_PIN, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Falha ao configurar interrupção do botão.");
        return 0;
    }
    // Adiciona a função de callback para a interrupção do botão
    gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(BUTTON_PIN));
    gpio_add_callback(gpioa_dev, &button_cb_data);

    /* * NOVO: 9.3. Configuração dos Pinos de Sincronização (PTB1/PTB2) 
     */
    if (!device_is_ready(gpiob_dev)) {
        LOG_ERR("Erro: GPIOB (PTB1/PTB2) não está pronto.");
        return 0;
    }

    /* 9.3.1. Configurar SYNC_OUT (PTB1) - Saída, Nível Alto (Inativo) */
    ret = gpio_pin_configure(gpiob_dev, SYNC_OUT_PIN, GPIO_OUTPUT_INACTIVE | GPIO_PULL_UP);
    if (ret < 0) {
        LOG_ERR("Erro %d ao configurar SYNC_OUT (PTB1)", ret);
        return 0;
    }
    gpio_pin_set(gpiob_dev, SYNC_OUT_PIN, 1); // Garante estado inativo ALTO

    /* 9.3.2. Configurar SYNC_IN (PTB2) - Entrada, Pull-up, Interrupção Borda de Descida */
    ret = gpio_pin_configure(gpiob_dev, SYNC_IN_PIN, (GPIO_INPUT | GPIO_PULL_UP));
    if (ret < 0) {
        LOG_ERR("Erro %d ao configurar SYNC_IN (PTB2)", ret);
        return 0;
    }
    ret = gpio_pin_interrupt_configure(gpiob_dev, SYNC_IN_PIN, GPIO_INT_EDGE_TO_ACTIVE); // (Ativo Baixo)
    if (ret < 0) {
        LOG_ERR("Erro %d ao configurar interrupção SYNC_IN", ret);
        return 0;
    }

    /* 9.3.3. Adicionar Callback para SYNC_IN (PTB2) */
    gpio_init_callback(&sync_in_cb_data, sync_in_callback, BIT(SYNC_IN_PIN));
    gpio_add_callback(gpiob_dev, &sync_in_cb_data);

    LOG_INF("Pinos de Sincronização (PTB1/PTB2) configurados.");


    /* 9.4. Define o Estado Inicial */
    if (g_night_mode) {
        LOG_INF("Semáforo Iniciado em MODO NOTURNO");
        current_state = STATE_NIGHT_BLINK;
        gpio_pin_set_dt(&led_green, 0); // Verde sempre desligado à noite
    } else {
        LOG_INF("Semáforo Iniciado em MODO NORMAL (Aguardando Botão)");
        current_state = STATE_RED_SOLID;
        gpio_pin_set_dt(&led_green, 0);
        gpio_pin_set_dt(&led_red, 1); // Estado padrão: Vermelho aceso
    }

    /* 9.5. Loop Principal (Máquina de Estados) */
    while (1) {
        switch (current_state) {

        case STATE_NIGHT_BLINK:
            // Lógica do Modo Noturno
            LOG_INF("Modo Noturno: Vermelho ON");
            gpio_pin_set_dt(&led_red, 1);
            k_sleep(K_SECONDS(1));
            
            if (g_night_mode) { // Checa se ainda está em modo noturno
                LOG_INF("Modo Noturno: Vermelho OFF");
                gpio_pin_set_dt(&led_red, 0);
                k_sleep(K_SECONDS(1));
            }

            // Checa se o modo mudou
            if (!g_night_mode) {
                LOG_INF("Mudando para MODO NORMAL.");
                current_state = STATE_RED_SOLID;
                gpio_pin_set_dt(&led_red, 1); // Acende o vermelho
                
                // Limpa semáforos caso algo tenha ocorrido durante a transição
                k_sem_reset(&button_sem);
                k_sem_reset(&safe_to_cross_sem);
            }
            break;

        case STATE_RED_SOLID:
            // Lógica do Modo Normal (Padrão)
            if (g_night_mode) { // Checa se o modo mudou
                LOG_INF("Mudando para MODO NOTURNO.");
                current_state = STATE_NIGHT_BLINK;
                gpio_pin_set_dt(&led_red, 0); // Apaga o vermelho antes de piscar
                break;
            }

            // Fica "preso" aqui esperando o semáforo do BOTÃO (PTA1)
            LOG_INF("Estado: Vermelho Sólido. Esperando botão (PTA1)...");
            k_sem_take(&button_sem, K_FOREVER);
            
            // Se chegamos aqui, o botão foi pressionado (e não estamos no modo noturno)
            
            /* * NOVO: Enviar pulso de "REQUEST" (via SYNC_OUT - PTB1)
             */
            LOG_INF("Botão recebido! Enviando PEDIDO (SYNC_OUT PTB1) para veículos.");
            gpio_pin_set(gpiob_dev, SYNC_OUT_PIN, 0); // Nível BAIXO (Ativo)
            k_msleep(100); // Duração do pulso
            gpio_pin_set(gpiob_dev, SYNC_OUT_PIN, 1); // Nível ALTO (Inativo via Pull-up)

            LOG_INF("Pedido enviado. Aguardando sinal SEGURO (SYNC_IN PTB2)...");
            current_state = STATE_WAITING_FOR_SAFE; // Mudar para o novo estado de espera
            break;

        /* NOVO ESTADO DE ESPERA */
        case STATE_WAITING_FOR_SAFE:
            /* Espera pelo sinal "SEGURO" (safe_to_cross_sem) vindo do veículo (via PTB2) */

            // Checa se o modo noturno foi ativado *enquanto* esperava
            if (g_night_mode) { 
                LOG_INF("Modo noturno ativado enquanto esperava. Cancelando pedido.");
                current_state = STATE_NIGHT_BLINK;
                gpio_pin_set_dt(&led_red, 0); 
                k_sem_reset(&safe_to_cross_sem); // Limpa o semáforo caso o sinal chegue tarde
                break;
            }

            // Tenta pegar o semáforo (liberado pela ISR do SYNC_IN - PTB2)
            // Usamos um timeout de 200ms para checar o modo noturno periodicamente
            if (k_sem_take(&safe_to_cross_sem, K_MSEC(200)) == 0) {
                // Sucesso! Sinal recebido.
                LOG_INF("Sinal SEGURO (SYNC_IN) recebido! Iniciando ciclo verde.");
                current_state = STATE_GREEN_CYCLE;
            }
            
            // Se k_sem_take falhar (timeout), o loop principal roda de novo,
            // entra neste case, checa g_night_mode, e tenta pegar o semáforo novamente.
            break;

        case STATE_GREEN_CYCLE:
            // Executa o ciclo de pedestre
            
            // "imediatamente verde"
            LOG_INF("Ciclo: VERDE (Ande) ON por 4s");
            gpio_pin_set_dt(&led_red, 0);
            gpio_pin_set_dt(&led_green, 1);
            k_sleep(K_SECONDS(4));

            // "continue o ciclo"
            LOG_INF("Ciclo: VERDE OFF");
            gpio_pin_set_dt(&led_green, 0);
            LOG_INF("Ciclo: VERMELHO (Limpeza) ON por 2s");
            gpio_pin_set_dt(&led_red, 1);
            k_sleep(K_SECONDS(2));

            // Ciclo completo, volta ao estado padrão
            LOG_INF("Ciclo completo. Voltando ao estado Vermelho Sólido.");
            
            // Limpa qualquer clique de botão (PTA1) que tenha ocorrido durante o ciclo
            k_sem_reset(&button_sem);
            // Limpa o semáforo de "seguro" (PTB2)
            k_sem_reset(&safe_to_cross_sem);
            
            current_state = STATE_RED_SOLID;
            // O LED vermelho já está aceso, pronto para o estado STATE_RED_SOLID
            break;
        }

        // Pequeno sleep para evitar que o loop rode "solto"
        k_sleep(K_MSEC(10));
    }

    return 0; // Nunca alcançado
}
/* [FIM - CÓDIGO SEMÁFORO DE PEDESTRES MODIFICADO] */