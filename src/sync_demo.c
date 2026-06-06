#define _POSIX_C_SOURCE 200809L
#include "os_project.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

/*
 * Synchronization module.
 * Demonstrates producer-consumer, readers-writers, and dining philosophers
 * with POSIX threads, mutexes, and semaphores.
 */

#define MAX_SYNC_WORKERS 16
#define MAX_SYNC_BUFFER 64
#define STOP_ITEM (-1)

typedef struct {
    int buffer[MAX_SYNC_BUFFER];
    int capacity;
    int in;
    int out;
    int produced;
    int consumed;
    int items_per_producer;
    sem_t empty;
    sem_t full;
    pthread_mutex_t mutex;
    pthread_mutex_t log_mutex;
} PCContext;

typedef struct {
    PCContext *ctx;
    int id;
} PCArg;

typedef struct {
    int reader_count;
    int value;
    int read_rounds;
    int write_rounds;
    pthread_mutex_t reader_mutex;
    pthread_mutex_t resource_mutex;
    pthread_mutex_t log_mutex;
} RWContext;

typedef struct {
    RWContext *ctx;
    int id;
} RWArg;

typedef struct {
    int philosophers;
    int rounds;
    int meals[MAX_SYNC_WORKERS];
    pthread_mutex_t forks[MAX_SYNC_WORKERS];
    pthread_mutex_t log_mutex;
    sem_t room;
} DPContext;

typedef struct {
    DPContext *ctx;
    int id;
} DPArg;

static void short_pause(void) {
    const struct timespec ts = {0, 1000000L};
    (void)nanosleep(&ts, NULL);
}

static void locked_printf(pthread_mutex_t *mutex, const char *fmt, ...) {
    va_list args;
    pthread_mutex_lock(mutex);
    va_start(args, fmt);
    (void)vprintf(fmt, args);
    va_end(args);
    pthread_mutex_unlock(mutex);
}

static void pc_put(PCContext *ctx, int item) {
    /* empty/full semaphores model bounded-buffer capacity and available items. */
    sem_wait(&ctx->empty);
    pthread_mutex_lock(&ctx->mutex);
    ctx->buffer[ctx->in] = item;
    ctx->in = (ctx->in + 1) % ctx->capacity;
    pthread_mutex_unlock(&ctx->mutex);
    sem_post(&ctx->full);
}

static int pc_take(PCContext *ctx) {
    int item;
    /* Consumers block on full until a producer publishes an item or stop token. */
    sem_wait(&ctx->full);
    pthread_mutex_lock(&ctx->mutex);
    item = ctx->buffer[ctx->out];
    ctx->out = (ctx->out + 1) % ctx->capacity;
    pthread_mutex_unlock(&ctx->mutex);
    sem_post(&ctx->empty);
    return item;
}

static void *producer_thread(void *arg) {
    PCArg *producer = (PCArg *)arg;
    int i;
    for (i = 0; i < producer->ctx->items_per_producer; i++) {
        int item = producer->id * 100 + i;
        pc_put(producer->ctx, item);
        pthread_mutex_lock(&producer->ctx->mutex);
        producer->ctx->produced++;
        pthread_mutex_unlock(&producer->ctx->mutex);
        locked_printf(&producer->ctx->log_mutex, "producer-%d produce item=%d\n", producer->id, item);
        short_pause();
    }
    return NULL;
}

static void *consumer_thread(void *arg) {
    PCArg *consumer = (PCArg *)arg;
    for (;;) {
        int item = pc_take(consumer->ctx);
        if (item == STOP_ITEM) {
            locked_printf(&consumer->ctx->log_mutex, "consumer-%d stop\n", consumer->id);
            break;
        }
        pthread_mutex_lock(&consumer->ctx->mutex);
        consumer->ctx->consumed++;
        pthread_mutex_unlock(&consumer->ctx->mutex);
        locked_printf(&consumer->ctx->log_mutex, "consumer-%d consume item=%d\n", consumer->id, item);
        short_pause();
    }
    return NULL;
}

void run_producer_consumer_demo(int producers, int consumers, int items_per_producer, int buffer_size) {
    PCContext ctx;
    pthread_t producer_threads[MAX_SYNC_WORKERS];
    pthread_t consumer_threads[MAX_SYNC_WORKERS];
    PCArg producer_args[MAX_SYNC_WORKERS];
    PCArg consumer_args[MAX_SYNC_WORKERS];
    int i;

    if (producers <= 0) {
        producers = 2;
    }
    if (consumers <= 0) {
        consumers = 2;
    }
    if (items_per_producer < 0) {
        items_per_producer = 3;
    }
    if (buffer_size <= 0) {
        buffer_size = 3;
    }
    if (producers > MAX_SYNC_WORKERS) {
        producers = MAX_SYNC_WORKERS;
    }
    if (consumers > MAX_SYNC_WORKERS) {
        consumers = MAX_SYNC_WORKERS;
    }
    if (buffer_size > MAX_SYNC_BUFFER) {
        buffer_size = MAX_SYNC_BUFFER;
    }

    ctx.capacity = buffer_size;
    ctx.in = 0;
    ctx.out = 0;
    ctx.produced = 0;
    ctx.consumed = 0;
    ctx.items_per_producer = items_per_producer;
    sem_init(&ctx.empty, 0, (unsigned int)buffer_size);
    sem_init(&ctx.full, 0, 0);
    pthread_mutex_init(&ctx.mutex, NULL);
    pthread_mutex_init(&ctx.log_mutex, NULL);

    printf("生产者-消费者: producers=%d consumers=%d buffer=%d\n", producers, consumers, buffer_size);
    for (i = 0; i < consumers; i++) {
        consumer_args[i].ctx = &ctx;
        consumer_args[i].id = i;
        pthread_create(&consumer_threads[i], NULL, consumer_thread, &consumer_args[i]);
    }
    for (i = 0; i < producers; i++) {
        producer_args[i].ctx = &ctx;
        producer_args[i].id = i;
        pthread_create(&producer_threads[i], NULL, producer_thread, &producer_args[i]);
    }
    for (i = 0; i < producers; i++) {
        pthread_join(producer_threads[i], NULL);
    }
    for (i = 0; i < consumers; i++) {
        pc_put(&ctx, STOP_ITEM);
    }
    for (i = 0; i < consumers; i++) {
        pthread_join(consumer_threads[i], NULL);
    }
    printf("结果: produced=%d consumed=%d buffer_empty=yes\n", ctx.produced, ctx.consumed);

    pthread_mutex_destroy(&ctx.mutex);
    pthread_mutex_destroy(&ctx.log_mutex);
    sem_destroy(&ctx.empty);
    sem_destroy(&ctx.full);
}

static void *reader_thread(void *arg) {
    RWArg *reader = (RWArg *)arg;
    int round;
    for (round = 0; round < reader->ctx->read_rounds; round++) {
        pthread_mutex_lock(&reader->ctx->reader_mutex);
        /* The first reader locks the resource; the last reader releases it. */
        reader->ctx->reader_count++;
        if (reader->ctx->reader_count == 1) {
            pthread_mutex_lock(&reader->ctx->resource_mutex);
        }
        locked_printf(&reader->ctx->log_mutex, "reader-%d enter active_readers=%d\n", reader->id, reader->ctx->reader_count);
        pthread_mutex_unlock(&reader->ctx->reader_mutex);

        locked_printf(&reader->ctx->log_mutex, "reader-%d read value=%d\n", reader->id, reader->ctx->value);
        short_pause();

        pthread_mutex_lock(&reader->ctx->reader_mutex);
        reader->ctx->reader_count--;
        locked_printf(&reader->ctx->log_mutex, "reader-%d leave active_readers=%d\n", reader->id, reader->ctx->reader_count);
        if (reader->ctx->reader_count == 0) {
            pthread_mutex_unlock(&reader->ctx->resource_mutex);
        }
        pthread_mutex_unlock(&reader->ctx->reader_mutex);
        short_pause();
    }
    return NULL;
}

static void *writer_thread(void *arg) {
    RWArg *writer = (RWArg *)arg;
    int round;
    for (round = 0; round < writer->ctx->write_rounds; round++) {
        pthread_mutex_lock(&writer->ctx->resource_mutex);
        writer->ctx->value++;
        locked_printf(&writer->ctx->log_mutex, "writer-%d write value=%d\n", writer->id, writer->ctx->value);
        short_pause();
        pthread_mutex_unlock(&writer->ctx->resource_mutex);
        short_pause();
    }
    return NULL;
}

void run_readers_writers_demo(int readers, int writers, int read_rounds, int write_rounds) {
    RWContext ctx;
    pthread_t reader_threads[MAX_SYNC_WORKERS];
    pthread_t writer_threads[MAX_SYNC_WORKERS];
    RWArg reader_args[MAX_SYNC_WORKERS];
    RWArg writer_args[MAX_SYNC_WORKERS];
    int i;

    if (readers <= 0) {
        readers = 3;
    }
    if (writers <= 0) {
        writers = 2;
    }
    if (read_rounds < 0) {
        read_rounds = 2;
    }
    if (write_rounds < 0) {
        write_rounds = 2;
    }
    if (readers > MAX_SYNC_WORKERS) {
        readers = MAX_SYNC_WORKERS;
    }
    if (writers > MAX_SYNC_WORKERS) {
        writers = MAX_SYNC_WORKERS;
    }

    ctx.reader_count = 0;
    ctx.value = 0;
    ctx.read_rounds = read_rounds;
    ctx.write_rounds = write_rounds;
    pthread_mutex_init(&ctx.reader_mutex, NULL);
    pthread_mutex_init(&ctx.resource_mutex, NULL);
    pthread_mutex_init(&ctx.log_mutex, NULL);

    printf("读者-写者: readers=%d writers=%d\n", readers, writers);
    for (i = 0; i < readers; i++) {
        reader_args[i].ctx = &ctx;
        reader_args[i].id = i;
        pthread_create(&reader_threads[i], NULL, reader_thread, &reader_args[i]);
    }
    for (i = 0; i < writers; i++) {
        writer_args[i].ctx = &ctx;
        writer_args[i].id = i;
        pthread_create(&writer_threads[i], NULL, writer_thread, &writer_args[i]);
    }
    for (i = 0; i < readers; i++) {
        pthread_join(reader_threads[i], NULL);
    }
    for (i = 0; i < writers; i++) {
        pthread_join(writer_threads[i], NULL);
    }
    printf("结果: final_value=%d expected_writes=%d active_readers=%d\n", ctx.value, writers * write_rounds, ctx.reader_count);

    pthread_mutex_destroy(&ctx.reader_mutex);
    pthread_mutex_destroy(&ctx.resource_mutex);
    pthread_mutex_destroy(&ctx.log_mutex);
}

static void *philosopher_thread(void *arg) {
    DPArg *philosopher = (DPArg *)arg;
    DPContext *ctx = philosopher->ctx;
    int left = philosopher->id;
    int right = (philosopher->id + 1) % ctx->philosophers;
    int round;

    for (round = 0; round < ctx->rounds; round++) {
        locked_printf(&ctx->log_mutex, "philosopher-%d think round=%d\n", philosopher->id, round);
        short_pause();
        /* Limit room to N-1 philosophers to break circular wait and avoid deadlock. */
        sem_wait(&ctx->room);
        pthread_mutex_lock(&ctx->forks[left]);
        locked_printf(&ctx->log_mutex, "philosopher-%d pick fork %d\n", philosopher->id, left);
        pthread_mutex_lock(&ctx->forks[right]);
        locked_printf(&ctx->log_mutex, "philosopher-%d eat with forks %d,%d\n", philosopher->id, left, right);
        ctx->meals[philosopher->id]++;
        short_pause();
        pthread_mutex_unlock(&ctx->forks[right]);
        pthread_mutex_unlock(&ctx->forks[left]);
        sem_post(&ctx->room);
        locked_printf(&ctx->log_mutex, "philosopher-%d release forks\n", philosopher->id);
    }
    return NULL;
}

void run_dining_philosophers_demo(int philosophers, int rounds) {
    DPContext ctx;
    pthread_t threads[MAX_SYNC_WORKERS];
    DPArg args[MAX_SYNC_WORKERS];
    int i;
    int deadlock_free = 1;

    if (philosophers < 2) {
        philosophers = 5;
    }
    if (rounds < 0) {
        rounds = 2;
    }
    if (philosophers > MAX_SYNC_WORKERS) {
        philosophers = MAX_SYNC_WORKERS;
    }
    ctx.philosophers = philosophers;
    ctx.rounds = rounds;
    pthread_mutex_init(&ctx.log_mutex, NULL);
    sem_init(&ctx.room, 0, (unsigned int)(philosophers - 1));
    for (i = 0; i < philosophers; i++) {
        ctx.meals[i] = 0;
        pthread_mutex_init(&ctx.forks[i], NULL);
    }

    printf("哲学家进餐: philosophers=%d rounds=%d\n", philosophers, rounds);
    for (i = 0; i < philosophers; i++) {
        args[i].ctx = &ctx;
        args[i].id = i;
        pthread_create(&threads[i], NULL, philosopher_thread, &args[i]);
    }
    for (i = 0; i < philosophers; i++) {
        pthread_join(threads[i], NULL);
    }
    printf("结果: meals=");
    for (i = 0; i < philosophers; i++) {
        printf("%d%s", ctx.meals[i], i == philosophers - 1 ? "" : ",");
        if (ctx.meals[i] != rounds) {
            deadlock_free = 0;
        }
    }
    printf(" deadlock_free=%s\n", deadlock_free ? "yes" : "no");

    for (i = 0; i < philosophers; i++) {
        pthread_mutex_destroy(&ctx.forks[i]);
    }
    pthread_mutex_destroy(&ctx.log_mutex);
    sem_destroy(&ctx.room);
}
