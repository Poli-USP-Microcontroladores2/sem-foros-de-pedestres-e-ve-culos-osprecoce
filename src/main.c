/* Alterações marcadas com comentário "SYNC MOD" */

/* Código original do semáforo de veículos com adição de sincronização via PTB1/PTB2 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

/* Mapeamento FRDM-KL25Z:
   led0 = Verde
   led2 = Vermelho
*/
#define LED_GREEN_NODE   DT_ALIAS(led0)
#define LED_RED_NODE     DT_ALIAS(led2)

/* Definição do Botão PTA1 */
#define BUTTON_PORT  DT_NODELABEL(gpioa)
#define BUTTON_PIN   1

static const struct device *button_dev = DEVICE_DT_GET(BUTTON_PORT);

#if !DT_NODE_HAS_STATUS(LED_GREEN_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED_RED_NODE, okay)
#error "Faltam alias led0 ou led2 no Device Tree"
#endif

static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);

/* Semáforos para controlar a sequência */
K_SEM_DEFINE(sem_green, 1, 1);
K_SEM_DEFINE(sem_yellow, 0, 1);
K_SEM_DEFINE(sem_red, 0, 1);

/* Mutex para controlar LEDs */
K_MUTEX_DEFINE(led_mutex);

/* Flag do modo noturno */
static atomic_t night_mode = ATOMIC_INIT(0);   // 0=normal, 1=noturno

/* Flag de pedido de travessia (pedestre) e indicador de pedido em andamento */
static atomic_t ped_request = ATOMIC_INIT(0);  // 0=nenhum, 1=pedido pendente
static atomic_t ped_active  = ATOMIC_INIT(0);  // 0=nenhum, 1=em progresso (RED pedestre)

/* Estado atual do semáforo (para decisões ao receber pedido)
   0 = GREEN, 1 = YELLOW, 2 = RED
*/
static atomic_t current_state = ATOMIC_INIT(0);

/* Funções para controlar flags */
void set_night_mode(bool enable)
{
    atomic_set(&night_mode, enable ? 1 : 0);
    if (enable) {
        printk("Modo noturno ativado.\n");
    } else {
        printk("Modo noturno desativado.\n");
    }
}

/* Solicita travessia de pedestres (por flag). Retorna true se aceito, false se ignorado.
   Regras implementadas:
   - Ignora em modo noturno
   - Ignora se já existe ped_active (já atendendo)
   - Aceita apenas se estado atual for GREEN ou YELLOW
   - Se aceito, marca ped_request = 1; NOTA: comportamento específico (interromper green / aguardar yellow) é
     implementado nas threads que consultam estas flags.
*/
bool request_pedestrian_crossing(void)
{
    /* Ignorar pedidos no modo noturno */
    if (atomic_get(&night_mode)) {
        printk("Pedido de pedestre ignorado: modo noturno ativo.\n");
        return false;
    }

    /* Ignorar se já existe um atendimento em andamento */
    if (atomic_get(&ped_active)) {
        printk("Pedido de pedestre ignorado: já em progresso.\n");
        return false;
    }

    int state = atomic_get(&current_state);
    if (!(state == 0 || state == 1)) {
        /* Se não estivermos em GREEN (0) ou YELLOW (1), ignorar */
        printk("Pedido de pedestre ignorado: estado atual não permite atendimento (estado=%d).\n", state);
        return false;
    }

    /* Aceitar pedido: marcar ped_request (será consumido pela sequência apropriada) */
    atomic_set(&ped_request, 1);
    printk("Pedido de travessia recebido (flag set). Estado atual=%d\n", state);
    return true;
}

/* Desliga LEDs */
void leds_off(void)
{
    gpio_pin_set_dt(&led_green, 0);
    gpio_pin_set_dt(&led_red,  0);
}

/* helper: espera total_ms dividindo em fatias e retorna cedo se night_mode ou ped_request ocorrerem
   -> Esta função não altera flags; o comportamento de reação ficará nas threads.
*/
static void sleep_chunked_check(uint32_t total_ms)
{
    const uint32_t CHUNK_MS = 100; // checagem a cada 100ms para permitir interrupção rápida
    uint32_t elapsed = 0;

    while (elapsed < total_ms) {
        uint32_t t = MIN(CHUNK_MS, total_ms - elapsed);
        k_msleep(t);
        elapsed += t;
        /* interrompe retorno ao chamador para que ele verifique flags */
        break; // NOTE: voltamos ao chamador para que este checar flags explicitamente
    }
}

/* Versão utilizada nas threads: dorme em fatias e permite checagens externas a cada fatia */
static bool sleep_with_checks(uint32_t total_ms)
{
    const uint32_t CHUNK_MS = 100;
    uint32_t elapsed = 0;

    while (elapsed < total_ms) {
        uint32_t t = MIN(CHUNK_MS, total_ms - elapsed);
        k_msleep(t);
        elapsed += t;

        /* Se o modo noturno for ativado, retornamos true para indicar interrupção */
        if (atomic_get(&night_mode)) {
            return true; // interrupção por modo noturno
        }
        /* Se ped_request apareceu, retornamos true para indicar interrupção */
        if (atomic_get(&ped_request)) {
            return true; // interrupção por pedido pedestre
        }
    }
    return false; // completou sem interrupções
}

/* =========================
   SYNC (SINCRONIZAÇÃO) - MODIFICAÇÕES
   =========================
   - SYNC_OUT : PTB1 (saída digital)
   - SYNC_IN  : PTB2 (entrada com interrupção, pull-down)
   - Estratégia: pulso ativo ALTO (~200ms). Entradas com pull-down, interrupção na borda de subida.
   - Quando SYNC_IN é recebido (de placa pedestre), marcamos ped_request (se permitido).
   - Quando ciclo de travessia (RED pedestre) terminar, a placa veículos envia um pulso de confirmação via SYNC_OUT.
*/

/* SYNC on PORTB */
#define SYNC_PORT   DT_NODELABEL(gpiob)  /* SYNC MOD: porta B */
#define SYNC_OUT_PIN 1                    /* PTB1 - saída (gera pulsos) */
#define SYNC_IN_PIN  2                    /* PTB2 - entrada com interrupção */

static const struct device *sync_dev = DEVICE_DT_GET(SYNC_PORT);
static struct gpio_callback sync_in_cb_data;

/* Envia um pulso ativo (HIGH) por ~200ms no SYNC_OUT (bloqueante curto) */
static void send_sync_pulse(void)
{
    if (!device_is_ready(sync_dev)) {
        printk("SYNC: dispositivo PORTB não pronto para enviar pulso.\n");
        return;
    }

    /* Se em modo noturno, não envia sinal (requisito: modo noturno ignora sincronização) */
    if (atomic_get(&night_mode)) {
        printk("SYNC: modo noturno ativo - não envia pulso.\n");
        return;
    }

    gpio_pin_set(sync_dev, SYNC_OUT_PIN, 1);
    /* pequeno delay (200ms) para garantir detecção */
    k_msleep(200);
    gpio_pin_set(sync_dev, SYNC_OUT_PIN, 0);

    printk("SYNC: pulso de confirmação enviado (SYNC_OUT PTB1).\n");
}

/* Callback da entrada SYNC_IN (recebe sinal da placa pedestre) */
void sync_in_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(dev);

    /* Ignorar se modo noturno ativo */
    if (atomic_get(&night_mode)) {
        printk("SYNC: sinal recebido mas ignorado (modo noturno ativo).\n");
        return;
    }

    /* Ao receber sinal do pedestre, marcamos ped_request se a regra permitir (igual a request_pedestrian_crossing) */
    int state = atomic_get(&current_state);
    if (!(state == 0 || state == 1)) {
        printk("SYNC: sinal recebido, porém estado atual não aceita atendimento (estado=%d). Ignorando.\n", state);
        return;
    }

    /* Marca pedido - a máquina de estados consumirá essa flag como se fosse um botão */
    atomic_set(&ped_request, 1);
    printk("SYNC: sinal recebido do pedestre - ped_request setado (estado atual=%d).\n", state);
}

/* =========================
   Fim das modificações SYNC
   ========================= */

/* GREEN THREAD */
void green_thread(void)
{
    while (1) {

        if (atomic_get(&night_mode)) {
            k_msleep(100);
            continue;
        }

        k_sem_take(&sem_green, K_FOREVER);

        /* Setar estado atual para GREEN */
        atomic_set(&current_state, 0);

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_green, 1);

        /* dormir 3000ms em fatias, verificando flags */
        bool interrupted = sleep_with_checks(3000);

        if (interrupted) {
            /* Se entra aqui por modo noturno, apenas desligar e aguardar loop superior */
            if (atomic_get(&night_mode)) {
                gpio_pin_set_dt(&led_green, 0);
                k_mutex_unlock(&led_mutex);
                continue;
            }

            /* Interrompido por pedido pedestre *ENQUANTO EM GREEN* */
            if (atomic_get(&ped_request)) {
                /* Não consumimos ped_request aqui: queremos que o fluxo faça AMARELO (1s) e
                   depois RED pedestre (4s). Portanto, somente sinalizamos a transição para YELLOW.
                */
                gpio_pin_set_dt(&led_green, 0);
                k_mutex_unlock(&led_mutex);

                printk("Green interrompido por pedido: irá para YELLOW (1s) então RED pedestre.\n");

                /* indicar que próxima fase é YELLOW */
                atomic_set(&current_state, 1);
                k_sem_give(&sem_yellow);
                continue;
            }
        }

        /* tempo normal completo */
        gpio_pin_set_dt(&led_green, 0);
        k_mutex_unlock(&led_mutex);

        k_sem_give(&sem_yellow);
    }
}

/* YELLOW THREAD (modo normal) */
void yellow_thread(void)
{
    while (1) {

        if (atomic_get(&night_mode)) {
            k_msleep(100);
            continue;
        }

        k_sem_take(&sem_yellow, K_FOREVER);

        /* Setar estado atual para YELLOW */
        atomic_set(&current_state, 1);

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        /* representação: amarelo = ambos LEDs acesos */
        gpio_pin_set_dt(&led_green, 1);
        gpio_pin_set_dt(&led_red, 1);

        /* piscar amarelo no modo normal (aqui amarelo é 1s) = 1000ms
           Importante: se ped_request ocorrer DURANTE o amarelo, NÃO reiniciamos o amarelo;
           deixamos terminar e depois vamos para RED pedestre.
        */
        const uint32_t TOTAL_YELLOW_MS = 1000;
        uint32_t elapsed = 0;
        const uint32_t CHUNK_MS = 100;
        bool early_night = false;

        while (elapsed < TOTAL_YELLOW_MS) {
            uint32_t t = MIN(CHUNK_MS, TOTAL_YELLOW_MS - elapsed);
            k_msleep(t);
            elapsed += t;

            if (atomic_get(&night_mode)) {
                early_night = true;
                break;
            }
            /* Note: não agimos imediatamente se ped_request aparece — apenas registramos que há um pedido
               e após o amarelo terminar o trataremos.
            */
        }

        if (early_night) {
            /* se modo noturno ativado, desligar e liberar mutex */
            leds_off();
            k_mutex_unlock(&led_mutex);
            continue;
        }

        /* Após completar o amarelo (ou terminar o tempo restante), verificar se há pedido pedestre pendente */
        if (atomic_get(&ped_request) && !atomic_get(&ped_active) && !atomic_get(&night_mode)) {
            /* Consumir pedido e marcar atendimento pedestre */
            atomic_set(&ped_request, 0);
            atomic_set(&ped_active, 1);

            leds_off();
            k_mutex_unlock(&led_mutex);

            printk("Yellow terminou: iniciando RED pedestre por 4s.\n");
            k_sem_give(&sem_red);
            continue;
        }

        /* Sem pedido pendente: sequência normal -> RED */
        leds_off();
        k_mutex_unlock(&led_mutex);

        k_sem_give(&sem_red);
    }
}

/* RED THREAD */
void red_thread(void)
{
    while (1) {

        if (atomic_get(&night_mode)) {
            k_msleep(100);
            continue;
        }

        k_sem_take(&sem_red, K_FOREVER);

        /* Setar estado atual para RED */
        atomic_set(&current_state, 2);

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_red, 1);

        if (atomic_get(&ped_active)) {
            /* Atendimento de pedestre: permanecer 4s e ignorar novos pedidos nesse intervalo */
            printk("RED (pedestre) aceso por 4s. Novos pedidos ignorados.\n");
            const uint32_t CHUNK_MS = 100;
            uint32_t elapsed = 0;
            while (elapsed < 4000) {
                k_msleep(CHUNK_MS);
                elapsed += CHUNK_MS;
                /* ignorar ped_request enquanto ped_active == 1 */
            }

            /* Antes de finalizar atendimento, enviar confirmação de volta à placa pedestre */
            send_sync_pulse(); /* SYNC MOD: confirma ao pedestre que o ciclo terminou */

            /* finalizar atendimento pedestre */
            atomic_set(&ped_active, 0);
            printk("RED (pedestre) finalizado. Voltando ao ciclo normal (GREEN).\n");
            leds_off();
            k_mutex_unlock(&led_mutex);

            /* Reiniciar ciclo começando por green */
            k_sem_give(&sem_green);
            continue;
        } else {
            /* RED normal: 4s (como parte do ciclo) mas pode ser interrompido por pedido pedestre
               — então dormimos em fatias checando ped_request
            */
            bool interrupted = false;

            const uint32_t CHUNK_MS = 100;
            uint32_t elapsed = 0;
            while (elapsed < 4000) {
                k_msleep(CHUNK_MS);
                elapsed += CHUNK_MS;

                if (atomic_get(&night_mode)) {
                    interrupted = true;
                    break;
                }
                if (atomic_get(&ped_request)) {
                    /* Se ped_request surge durante RED normal, devemos IGNORAR conforme especificação */
                    /* Portanto NÃO consumimos ped_request aqui; apenas registramos que houve tentativa e
                       deixamos o ciclo normal completar. */
                    printk("Pedido de pedestre recebido durante RED normal: ignorado.\n");
                    /* opcional: clear ped_request para evitar tentativa futura? Não remover — assumir que
                       request_pedestrian_crossing já teria recusado se estado fosse RED. */
                }
            }

            leds_off();
            k_mutex_unlock(&led_mutex);

            if (atomic_get(&night_mode)) {
                /* Se entramos no modo noturno, o thread noturno cuidará do piscar */
                continue;
            }

            /* Segue sequencia normal */
            k_sem_give(&sem_green);
        }
    }
}

/* NIGHT MODE THREAD – Pisca amarelo */
void night_mode_thread(void)
{
    while (1) {
        if (!atomic_get(&night_mode)) {
            k_msleep(200);
            continue;
        }

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_green, 1);
        gpio_pin_set_dt(&led_red, 1); // amarelo
        k_msleep(1000);

        leds_off();
        k_mutex_unlock(&led_mutex);

        k_msleep(1000);
    }
}

void button_thread(void)
{
    /* Configuração do pino PTA1 */
    if (!device_is_ready(button_dev)) {
        printk("Erro: GPIOA não está pronto para o botão\n");
        return;
    }

    gpio_pin_configure(button_dev, BUTTON_PIN, GPIO_INPUT | GPIO_PULL_UP);

    uint8_t last_state = 1; // pull-up → repouso = 1

    while (1) {
        uint8_t state = gpio_pin_get(button_dev, BUTTON_PIN);

        /* Detecta borda de descida: 1 → 0 = botão pressionado */
        if (last_state == 1 && state == 0) {
            printk("Botão de pedestre (local) pressionado\n");
            request_pedestrian_crossing();
        }

        last_state = state;
        k_msleep(50); // polling a cada 50ms
    }
}

/* Cria threads */
K_THREAD_DEFINE(green_tid,  512, green_thread,      NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(yellow_tid, 512, yellow_thread,     NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(red_tid,    512, red_thread,        NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(night_tid,  512, night_mode_thread, NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(button_tid, 512, button_thread, NULL, NULL, NULL, 2, 0, 0);


void main(void)
{
    int ret;
    const struct gpio_dt_spec *leds[] = { &led_green, &led_red };

    for (int i = 0; i < 2; i++) {
        if (!gpio_is_ready_dt(leds[i])) {
            printk("Erro: LED %d não está pronto\n", i);
            return;
        }
        ret = gpio_pin_configure_dt(leds[i], GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            printk("Erro %d ao configurar LED %d\n", ret, i);
            return;
        }
    }

    /* SYNC MOD: configurar SYNC_OUT e SYNC_IN em PORTB */
    if (!device_is_ready(sync_dev)) {
        printk("Erro: PORTB não pronto para SYNC\n");
        /* prosseguimos sem sync para permitir testes locais */
    } else {
        ret = gpio_pin_configure(sync_dev, SYNC_OUT_PIN, GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            printk("Erro %d ao configurar SYNC_OUT PTB1\n", ret);
        }
        /* SYNC_IN: entrada com pull-down e interrupção na borda de subida (active HIGH pulse) */
        ret = gpio_pin_configure(sync_dev, SYNC_IN_PIN, GPIO_INPUT | GPIO_PULL_DOWN);
        if (ret < 0) {
            printk("Erro %d ao configurar SYNC_IN PTB2\n", ret);
        } else {
            ret = gpio_pin_interrupt_configure(sync_dev, SYNC_IN_PIN, GPIO_INT_EDGE_TO_ACTIVE);
            if (ret < 0) {
                printk("Erro %d ao configurar interrupção SYNC_IN\n", ret);
            } else {
                gpio_init_callback(&sync_in_cb_data, sync_in_callback, BIT(SYNC_IN_PIN));
                gpio_add_callback(sync_dev, &sync_in_cb_data);
                printk("SYNC: configurado PTB1 (OUT) / PTB2 (IN int)\n");
            }
        }
    }

    /* Garante que o ciclo inicie por green */
    atomic_set(&current_state, 0);
    k_sem_give(&sem_green);

    /* main fica em loop para não encerrar */
    while (1) {
        k_msleep(1000);
    }
}
