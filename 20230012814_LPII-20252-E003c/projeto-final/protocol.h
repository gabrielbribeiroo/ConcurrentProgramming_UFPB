#ifndef PROTOCOL_H
#define PROTOCOL_H

/* =========================================================
 * protocol.h — Definições compartilhadas do protocolo IoT
 * ========================================================= */

#include <stdint.h>

/* ---------- Portas padrão ---------- */
#define DEFAULT_TCP_PORT  9000
#define DEFAULT_UDP_PORT  9001

/* ---------- Limites ---------- */
#define MAX_BUF          4096
#define MAX_SENSORS      3
#define MAX_SENSOR_NAME  32
#define MAX_MSG_LEN      512

/* ---------- Intervalos de envio (µs) ---------- */
#define INTERVAL_TEMPERATURA  100000
#define INTERVAL_UMIDADE      200000
#define INTERVAL_PRESSAO      500000

/* ---------- Faixas de valores ---------- */
#define TEMP_MIN   15.0
#define TEMP_MAX   40.0
#define UMID_MIN   30.0
#define UMID_MAX   90.0
#define PRES_MIN  990.0
#define PRES_MAX 1030.0

/* ---------- Tipos de sensor ---------- */
typedef enum {
    SENSOR_TEMPERATURA = 0,
    SENSOR_UMIDADE     = 1,
    SENSOR_PRESSAO     = 2
} SensorType;

/* ---------- Info de sensor (lookup table) ---------- */
typedef struct {
    const char *name;
    const char *unit;
    double      min_val;
    double      max_val;
    int         interval_us;
} SensorInfo;

static const SensorInfo SENSOR_TABLE[MAX_SENSORS] = {
    { "temperatura", "C",   TEMP_MIN, TEMP_MAX,  INTERVAL_TEMPERATURA },
    { "umidade",     "%",   UMID_MIN, UMID_MAX,  INTERVAL_UMIDADE     },
    { "pressao",     "hPa", PRES_MIN, PRES_MAX,  INTERVAL_PRESSAO     }
};

/* ---------- Códigos de status HTTP-like ---------- */
#define STATUS_OK        200
#define STATUS_BAD_REQ   400
#define STATUS_NOT_FOUND 404
#define STATUS_CONFLICT  409

/* ---------- Delimitador de mensagem ---------- */
#define MSG_END "\r\n\r\n"

#endif /* PROTOCOL_H */
