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
static atomic_t ped_request = ATOMIC_INIT(0);  // 0=nenhum, 1=pedido
static atomic_t ped_active  = ATOMIC_INIT(0);  // 0=nenhum, 1=em progresso

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

/* Solicita travessia de pedestres (por flag). Retorna true se aceito, false se ignorado. */
bool request_pedestrian_crossing(void)
{
    /* Ignorar pedidos no modo noturno */
    if (atomic_get(&night_mode)) {
        printk("Pedido de pedestre ignorado: modo noturno ativo.\n");
        return false;
    }

    /* Ignorar se já existe um em andamento */
    if (atomic_get(&ped_active)) {
        printk("Pedido de pedestre ignorado: já em progresso.\n");
        return false;
    }

    /* Marcar pedido */
    atomic_set(&ped_request, 1);
    printk("Pedido de travessia recebido (flag set).\n");
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

/* GREEN THREAD */
void green_thread(void)
{
    while (1) {

        if (atomic_get(&night_mode)) {
            k_msleep(100);
            continue;
        }

        k_sem_take(&sem_green, K_FOREVER);

        /* Antes de acender, verificar se chegou pedido de pedestre */
        if (atomic_get(&ped_request) && !atomic_get(&night_mode)) {
            /* converter pedido em ativo e direcionar para red imediatamente */
            atomic_set(&ped_request, 0);
            atomic_set(&ped_active, 1);
            printk("Interrupção: green -> encaminhando para RED por pedido pedestre.\n");
            k_sem_give(&sem_red);
            continue; /* volta ao loop sem acender */
        }

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
                /* voltar — night_mode_thread ficará responsável */
                continue;
            }

            /* Interrompido por pedido pedestre */
            if (atomic_get(&ped_request)) {
                /* Prepara o estado de atendimento pedestre */
                atomic_set(&ped_request, 0);
                atomic_set(&ped_active, 1);
                gpio_pin_set_dt(&led_green, 0);
                k_mutex_unlock(&led_mutex);

                printk("Green interrompido: direcionando para RED (pedestre)\n");
                k_sem_give(&sem_red);
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

        /* Antes de acender, verificar pedido pedestre */
        if (atomic_get(&ped_request) && !atomic_get(&night_mode)) {
            atomic_set(&ped_request, 0);
            atomic_set(&ped_active, 1);
            printk("Interrupção: yellow -> encaminhando para RED por pedido pedestre.\n");
            k_sem_give(&sem_red);
            continue;
        }

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_green, 1);
        gpio_pin_set_dt(&led_red, 1);  // amarelo

        /* piscar amarelo no modo normal (aqui amarelo é 1s) = 1000ms */
        bool interrupted = sleep_with_checks(1000);

        if (interrupted) {
            if (atomic_get(&night_mode)) {
                /* se modo noturno ativado, desligar e liberar mutex */
                leds_off();
                k_mutex_unlock(&led_mutex);
                continue;
            }
            if (atomic_get(&ped_request)) {
                atomic_set(&ped_request, 0);
                atomic_set(&ped_active, 1);
                leds_off();
                k_mutex_unlock(&led_mutex);
                printk("Yellow interrompido: direcionando para RED (pedestre)\n");
                k_sem_give(&sem_red);
                continue;
            }
        }

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

        /* Se ped_active já estiver marcado, este red é para pedestres.
           Caso ped_request esteja marcado (vindo de outra thread), consumi-lo aqui. */
        if (atomic_get(&ped_request) && !atomic_get(&ped_active) && !atomic_get(&night_mode)) {
            atomic_set(&ped_request, 0);
            atomic_set(&ped_active, 1);
            printk("Red recebeu pedido pedestre e assume o atendimento.\n");
        }

        k_mutex_lock(&led_mutex, K_FOREVER);
        leds_off();
        gpio_pin_set_dt(&led_red, 1);

        if (atomic_get(&ped_active)) {
            /* Atendimento de pedestre: permanecer 4s e ignorar novos pedidos nesse intervalo */
            printk("RED (pedestre) aceso por 4s. Novos pedidos ignorados.\n");
            /* dormir 4s em fatias, mas não responder a novos ped_request (eles serão ignorados) */
            const uint32_t CHUNK_MS = 100;
            uint32_t elapsed = 0;
            while (elapsed < 4000) {
                k_msleep(CHUNK_MS);
                elapsed += CHUNK_MS;
                /* ignorar ped_request enquanto ped_active == 1 */
            }
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
               — então dormimos em fatias checando ped_request */
            bool interrupted = false;

            /* dorme 4000ms checando ped_request/mode */
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
                    /* marca ped_active, consome ped_request e interrompe para atendimento */
                    atomic_set(&ped_request, 0);
                    atomic_set(&ped_active, 1);
                    interrupted = true;
                    break;
                }
            }

            leds_off();
            k_mutex_unlock(&led_mutex);

            if (atomic_get(&night_mode)) {
                /* Se entramos no modo noturno, o thread noturno cuidará do piscar */
                continue;
            }

            if (atomic_get(&ped_active)) {
                /* Se ped_active agora é 1, é porque foi acionado durante o red normal */
                /* Precisamos acender red por 4s adicionais (atendimento pedestre) */
                printk("RED interrompido para atender pedestre: completando 4s ped.\n");
                /* Re-entrar no red pedestre: dar o sem_red para que o fluxo entre novamente.
                   Mas como já estamos aqui, podemos tratar diretamente: */
                k_mutex_lock(&led_mutex, K_FOREVER);
                gpio_pin_set_dt(&led_red, 1);
                const uint32_t CHUNK_MS2 = 100;
                uint32_t elapsed2 = 0;
                while (elapsed2 < 4000) {
                    k_msleep(CHUNK_MS2);
                    elapsed2 += CHUNK_MS2;
                }
                atomic_set(&ped_active, 0);
                leds_off();
                k_mutex_unlock(&led_mutex);

                /* Após atendimento pedestre, reiniciar pelo verde */
                k_sem_give(&sem_green);
                continue;
            }

            /* Se não houve interrupção, segue sequencia normal */
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

/* Cria threads */
K_THREAD_DEFINE(green_tid,  512, green_thread,      NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(yellow_tid, 512, yellow_thread,     NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(red_tid,    512, red_thread,        NULL, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(night_tid,  512, night_mode_thread, NULL, NULL, NULL, 1, 0, 0);

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

    /* Ativa o modo noturno automaticamente ao iniciar */
    set_night_mode(true); // conforme requisito: iniciar em modo noturno

    printk("Semáforo iniciado diretamente em modo noturno!\n");

    /* Exemplo: desativa o modo noturno depois de 5s para demonstrar transição automática (opcional) */
    /* Comentário: remova ou altere conforme desejado; deixei para testes. */
    k_msleep(5000);
    set_night_mode(false);
    printk("Transição para modo normal após 5s (exemplo). Ciclo começará em GREEN.\n");
    /* Garante que o ciclo inicie por green */
    k_sem_give(&sem_green);

    /* Exemplo de trigger do pedestre por software após 10s (apenas demonstração) */
    /* Remova ou comente em produção; em uso real, chame request_pedestrian_crossing() a partir de um ISR de botão. */
    k_msleep(5000);
    printk("Simulando pedido de pedestre (flag) em 10s do boot.\n");
    request_pedestrian_crossing();

    /* main fica em loop para não encerrar */
    while (1) {
        k_msleep(1000);
    }
}
