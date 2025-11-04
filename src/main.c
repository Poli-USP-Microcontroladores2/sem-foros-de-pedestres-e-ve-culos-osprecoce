/* Alterações marcadas com comentário "SYNC MOD" */

/*
 * Semáforo de Pedestres com Botão, Máquina de Estados e Modo Noturno
 * Placa: FRDM-KL25Z
 *
 * Adicionado: sincronização com semáforo de veículos via PTB1 (OUT) / PTB2 (IN)
 * Estratégia de sinal: pulso ativo ALTO (~200ms) em SYNC_OUT (PTB1). Entrada SYNC_IN com pull-down e interrupt rising edge.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(semaforo_pedestre, LOG_LEVEL_DBG);

/* 1. Definição dos LEDs (Usando DT, pois são da placa) */

// Verde = led0
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
// Vermelho = led1
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);


/* 2. Definição Manual do Botão (PTA1) - Sem Overlay */

const struct device *gpioa_dev = DEVICE_DT_GET(DT_NODELABEL(gpioa));
#define BUTTON_PIN 1
static struct gpio_callback button_cb_data;

/* 3. Semáforo para sinalizar o evento do botão */
K_SEM_DEFINE(button_sem, 0, 1); // Liberado pela ISR do botão (ou pela lógica de envio)

/* 4. Flag de Controle do Modo Noturno */
static volatile bool g_night_mode = false;

/* 5. Estados da Máquina */
enum state {
    STATE_NIGHT_BLINK, // Modo Noturno: Piscando vermelho
    STATE_RED_SOLID,   // Modo Normal: Vermelho sólido (esperando botão)
    STATE_GREEN_CYCLE  // Modo Normal: Ciclo do verde (4s) + vermelho (2s)
};

/* =========================
   SYNC (SINCRONIZAÇÃO) - MODIFICAÇÕES
   =========================
   - SYNC_OUT : PTB1 (saída digital)
   - SYNC_IN  : PTB2 (entrada com interrupção, pull-down)
   - Estratégia: pulso ativo ALTO (~200ms). Entrada com pull-down detecta borda de subida.
   - Quando o botão é pressionado, a placa pedestre envia um pulso em SYNC_OUT para a placa veículos
     e **aguarda** um pulso de confirmação em SYNC_IN antes de iniciar seu ciclo verde.
   - Em modo noturno, a sincronização é ignorada.
*/

/* SYNC on PORTB */
#define SYNC_PORT   DT_NODELABEL(gpiob)  /* SYNC MOD: porta B */
#define SYNC_OUT_PIN 1                    /* PTB1 - saída (gera pulsos) */
#define SYNC_IN_PIN  2                    /* PTB2 - entrada com interrupção */

static const struct device *sync_dev = DEVICE_DT_GET(SYNC_PORT);
static struct gpio_callback sync_in_cb_data;

/* Semáforo que a ISR SYNC_IN dá quando recebe confirmação (placa veículos confirma seguro) */
K_SEM_DEFINE(sync_confirm_sem, 0, 1);

/* Envia um pulso ativo (HIGH) por ~200ms no SYNC_OUT (bloqueante curto) */
static void send_sync_pulse(void)
{
    if (!device_is_ready(sync_dev)) {
        LOG_ERR("SYNC: PORTB não pronto para enviar pulso.");
        return;
    }

    if (g_night_mode) {
        LOG_DBG("SYNC: modo noturno ativo - não envia pulso.");
        return;
    }

    gpio_pin_set(sync_dev, SYNC_OUT_PIN, 1);
    k_msleep(200); /* pulso de 200ms */
    gpio_pin_set(sync_dev, SYNC_OUT_PIN, 0);
    LOG_INF("SYNC: pulso enviado em SYNC_OUT (PTB1).");
}

/* Callback da entrada SYNC_IN (recebe confirmação da placa veículos) */
void sync_in_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(dev);

    if (g_night_mode) {
        LOG_DBG("SYNC: sinal recebido mas ignorado (modo noturno)");
        return;
    }

    /* Da ISR, apenas sinalizamos o semáforo de confirmação */
    k_sem_give(&sync_confirm_sem);
    LOG_INF("SYNC: confirmação recebida (SYNC_IN).");
}

/* =========================
   Fim das modificações SYNC
   ========================= */


/*
 * Função de Callback (ISR) do Botão
 *
 * Agora a ISR somente dá o semáforo button_sem; a thread principal fará:
 *  - enviar pulso para veículo via SYNC_OUT
 *  - aguardar confirmação via sync_confirm_sem
 *  - só então iniciar ciclo verde
 */
void button_pressed_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    /* Só sinaliza se NÃO estiver no modo noturno */
    if (!g_night_mode) {
        k_sem_give(&button_sem);
    }
}

int main(void)
{
    int ret;
    enum state current_state;

    /* LEDs via DT */
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        LOG_ERR("Dispositivo de LED não está pronto.");
        return 0;
    }
    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;
    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) return 0;

    /* Configuração do Botão (Manual, sem DT/Overlay) */
    if (!device_is_ready(gpioa_dev)) {
        LOG_ERR("Dispositivo GPIOA (PTA1) não está pronto.");
        return 0;
    }
    ret = gpio_pin_configure(gpioa_dev, BUTTON_PIN, (GPIO_INPUT | GPIO_PULL_UP));
    if (ret < 0) {
        LOG_ERR("Falha ao configurar botão PTA1.");
        return 0;
    }
    ret = gpio_pin_interrupt_configure(gpioa_dev, BUTTON_PIN, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret < 0) {
        LOG_ERR("Falha ao configurar interrupção do botão.");
        return 0;
    }
    gpio_init_callback(&button_cb_data, button_pressed_callback, BIT(BUTTON_PIN));
    gpio_add_callback(gpioa_dev, &button_cb_data);

    /* SYNC MOD: configurar SYNC_OUT e SYNC_IN em PORTB */
    if (!device_is_ready(sync_dev)) {
        LOG_ERR("SYNC: PORTB não pronto. Sincronização desabilitada.");
        /* prosseguimos sem sync (mas o código assume sync_dev pronto para testes) */
    } else {
        ret = gpio_pin_configure(sync_dev, SYNC_OUT_PIN, GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            LOG_ERR("Falha ao configurar SYNC_OUT PTB1: %d", ret);
        }
        /* SYNC_IN: entrada com pull-down e interrupção na borda de subida (active HIGH pulse) */
        ret = gpio_pin_configure(sync_dev, SYNC_IN_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
        if (ret < 0) {
            LOG_ERR("Falha ao configurar SYNC_IN PTB2: %d", ret);
        } else {
            ret = gpio_pin_interrupt_configure(sync_dev, SYNC_IN_PIN, GPIO_INT_EDGE_TO_ACTIVE);
            if (ret < 0) {
                LOG_ERR("Falha ao configurar interrupção SYNC_IN: %d", ret);
            } else {
                gpio_init_callback(&sync_in_cb_data, sync_in_callback, BIT(SYNC_IN_PIN));
                gpio_add_callback(sync_dev, &sync_in_cb_data);
                LOG_INF("SYNC: configurado PTB1 (OUT) / PTB2 (IN int)");
            }
        }
    }

    /* Estado Inicial */
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

    /* Loop Principal (Máquina de Estados) */
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

            // Espera botão (ISR libera button_sem)
            LOG_INF("Estado: Vermelho Sólido. Esperando botão...");
            k_sem_take(&button_sem, K_FOREVER);

            // Se chegamos aqui, o botão foi pressionado (e não estamos no modo noturno)
            LOG_INF("Botão recebido! Enviando pedido ao semáforo de veículos (SYNC_OUT) e aguardando confirmação...");
            /* SYNC MOD: enviar pulso para a placa de veículos solicitando travessia */
            send_sync_pulse();

            /* Aguardar confirmação da placa de veículos via SYNC_IN (semáforo sync_confirm_sem) */
            LOG_INF("Aguardando confirmação do semáforo de veículos (SYNC_IN)...");
            k_sem_take(&sync_confirm_sem, K_FOREVER); /* Bloqueia até receber confirmação */
            LOG_INF("Confirmação recebida! Iniciando ciclo verde.");

            current_state = STATE_GREEN_CYCLE;
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
            
            // Limpa qualquer clique de botão que tenha ocorrido durante o ciclo
            k_sem_reset(&button_sem); 
            
            current_state = STATE_RED_SOLID;
            // O LED vermelho já está aceso, pronto para o estado STATE_RED_SOLID
            break;
        }

        // Pequeno sleep para evitar que o loop rode "solto"
        k_sleep(K_MSEC(10));
    }

    return 0; // Nunca alcançado
}
