#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#define MAP_SIZE 100        // Карта больше в 5 раз
#define TILE_W 64
#define TILE_H 32
#define MAX_TREES 4000
#define MAX_BOTS 1000

typedef struct {
    Vector2 pos;
    float radius;
    Rectangle clickBounds;
} Tree;

typedef struct {
    Vector2 pos;
    Vector2 targetPos;
    float speed;
    float aiTimer;          // У каждого бота свое время до смены маршрута
    bool isVisible;         // Флаг видимости на экране
} Bot;

// Структура для очереди отрисовки (Y-сортировка)
typedef struct {
    Vector2 pos;
    int type;               // 0 - дерево, 1 - бот, 2 - игрок
    void* ptr;              // Указатель на сам объект для кастомизации
} RenderItem;

// Глобальные массивы
Tree trees[MAX_TREES];
Bot bots[MAX_BOTS];
Vector2 playerPos;
Camera2D camera = { 0 };

RenderItem renderQueue[MAX_TREES + MAX_BOTS + 1];
int renderQueueCount = 0;

Texture2D texAtlas; // Один атлас на всё! (Внутри: земля, дерево, игрок, бот)
// Координаты объектов в атласе (наш процедурный спрайтшит)
Rectangle recTile = { 0, 0, 64, 32 };
Rectangle recTree = { 64, 0, 64, 96 };
Rectangle recPlayer = { 128, 0, 32, 48 };
Rectangle recBot = { 160, 0, 32, 48 };

Vector2 GridToIso(Vector2 gridPos) {
    return (Vector2){ (gridPos.x - gridPos.y) * (TILE_W / 2.0f), (gridPos.x + gridPos.y) * (TILE_H / 2.0f) };
}

int CompareY(const void* a, const void* b) {
    RenderItem* itemA = (RenderItem*)a;
    RenderItem* itemB = (RenderItem*)b;
    float ya = (itemA->pos.x + itemA->pos.y) * (TILE_H / 2.0f);
    float yb = (itemB->pos.x + itemB->pos.y) * (TILE_H / 2.0f);
    return (ya > yb) - (ya < yb);
}

// Создаем текстурный атлас программно, чтобы не зависеть от файлов
void GenerateTextureAtlas(void) {
    Image atlas = GenImageColor(256, 128, BLANK);
    
    // Рисуем тайл земли (0, 0)
    ImageDrawLine(&atlas, 32, 0, 64, 16, DARKGREEN); ImageDrawLine(&atlas, 64, 16, 32, 32, DARKGREEN);
    ImageDrawLine(&atlas, 32, 32, 0, 16, DARKGREEN); ImageDrawLine(&atlas, 0, 16, 32, 0, DARKGREEN);
    ImageColorFill(&atlas, 32, 16, GREEN);

    // Рисуем дерево (64, 0)
    ImageDrawRectangle(&atlas, 64 + 28, 50, 8, 46, DARKBROWN);
    ImageDrawCircle(&atlas, 64 + 32, 32, 28, FORESTGREEN);

    // Рисуем игрока (128, 0)
    ImageDrawRectangle(&atlas, 128, 0, 32, 48, BLUE);
    ImageDrawCircle(&atlas, 128 + 16, 12, 10, ORANGE);

    // Рисуем бота (160, 0)
    ImageDrawRectangle(&atlas, 160, 0, 32, 48, PURPLE);
    ImageDrawCircle(&atlas, 160 + 16, 12, 10, MAROON);

    texAtlas = LoadTextureFromImage(atlas);
    UnloadImage(atlas);
}

int main(void) {
    InitWindow(1024, 768, "High Optimized Engine");
    SetTargetFPS(60);
    GenerateTextureAtlas();

    playerPos = (Vector2){ MAP_SIZE / 2.0f, MAP_SIZE / 2.0f };

    // Инициализация 4000 деревьев
    for (int i = 0; i < MAX_TREES; i++) {
        trees[i].pos = (Vector2){ (float)GetRandomValue(1, MAP_SIZE-2), (float)GetRandomValue(1, MAP_SIZE-2) };
        trees[i].radius = 0.3f;
    }

    // Инициализация 1000 Ботов
    for (int i = 0; i < MAX_BOTS; i++) {
        bots[i].pos = (Vector2){ (float)GetRandomValue(1, MAP_SIZE-2), (float)GetRandomValue(1, MAP_SIZE-2) };
        bots[i].targetPos = bots[i].pos;
        bots[i].speed = (float)GetRandomValue(10, 30) * 0.1f;
        bots[i].aiTimer = (float)GetRandomValue(0, 50) * 0.1f;
    }

    camera.offset = (Vector2){ GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f };
    camera.zoom = 1.0f;

    unsigned int frameCounter = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        frameCounter++;

        // 1. УПРАВЛЕНИЕ ИГРОКОМ
        Vector2 moveDir = { 0, 0 };
        if (IsKeyDown(KEY_W)) { moveDir.x -= 1; moveDir.y -= 1; }
        if (IsKeyDown(KEY_S)) { moveDir.x += 1; moveDir.y += 1; }
        if (IsKeyDown(KEY_A)) { moveDir.x -= 1; moveDir.y += 1; }
        if (IsKeyDown(KEY_D)) { moveDir.x += 1; moveDir.y -= 1; }
        if (Vector2Length(moveDir) > 0) {
            playerPos = Vector2Add(playerPos, Vector2Scale(Vector2Normalize(moveDir), 5.0f * dt));
        }

        camera.target = GridToIso(playerPos);

        // 2. ТАЙМ-СЛАЙСИНГ ИИ (Обновляем только часть ботов каждый кадр)
        // Делим 1000 ботов на 5 групп. Каждая группа обновляет ИИ раз в 5 кадров.
        int aiBatchSize = MAX_BOTS / 5;
        int startIndex = (frameCounter % 5) * aiBatchSize;
        int endIndex = startIndex + aiBatchSize;

        for (int i = 0; i < MAX_BOTS; i++) {
            // Перемещение (работает для всех каждый кадр, физика должна быть плавной)
            if (Vector2Distance(bots[i].pos, bots[i].targetPos) > 0.1f) {
                Vector2 dir = Vector2Normalize(Vector2Subtract(bots[i].targetPos, bots[i].pos));
                bots[i].pos = Vector2Add(bots[i].pos, Vector2Scale(dir, bots[i].speed * dt));
            }

            // А вот тяжелый выбор нового пути (рандом) — только для своей группы!
            if (i >= startIndex && i < endIndex) {
                bots[i].aiTimer -= dt * 5.0f; // Умножаем, так как обновляем реже
                if (bots[i].aiTimer <= 0.0f) {
                    bots[i].targetPos = (Vector2){ (float)GetRandomValue(1, MAP_SIZE-2), (float)GetRandomValue(1, MAP_SIZE-2) };
                    bots[i].aiTimer = (float)GetRandomValue(2, 6); // Смена пути раз в 2-6 сек
                }
            }
        }

        // ОБРАБОТКА КЛИКА ПО ДЕРЕВЬЯМ (Только по тем, что видны на экране!)
        Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), camera);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            for (int i = 0; i < MAX_TREES; i++) {
                // Если дерево на экране и мы кликнули в его хитбокс у земли
                if (CheckCollisionPointRec(mouseWorld, trees[i].clickBounds)) {
                    printf("Клик по Дереву ID: %d!\n", i);
                    break;
                }
            }
        }

        // 3. СБОР ОЧЕРЕДИ НА ОТРИСОВКУ + КЛЛИПИНГ (FRUSTUM CULLING)
        renderQueueCount = 0;
        
        // Экранные границы для проверки видимости (с запасом)
        float cullMargin = 150.0f;
        Rectangle screenRect = { 
            camera.target.x - (GetScreenWidth()/2.0f) - cullMargin, 
            camera.target.y - (GetScreenHeight()/2.0f) - cullMargin, 
            (float)GetScreenWidth() + cullMargin * 2, 
            (float)GetScreenHeight() + cullMargin * 2 
        };

        // Проверяем деревья
        for (int i = 0; i < MAX_TREES; i++) {
            Vector2 iso = GridToIso(trees[i].pos);
            // Хитбокс клика (ствол)
            trees[i].clickBounds = (Rectangle){ iso.x - 20, iso.y - 30, 40, 40 };

            // Если дерево в экране — пушим в сортировку
            if (CheckCollisionPointRec(iso, screenRect)) {
                renderQueue[renderQueueCount++] = (RenderItem){ trees[i].pos, 0, &trees[i] };
            }
        }

        // Проверяем ботов
        for (int i = 0; i < MAX_BOTS; i++) {
            Vector2 iso = GridToIso(bots[i].pos);
            if (CheckCollisionPointRec(iso, screenRect)) {
                renderQueue[renderQueueCount++] = (RenderItem){ bots[i].pos, 1, &bots[i] };
            }
        }

        // Пушим игрока (он всегда на экране)
        renderQueue[renderQueueCount++] = (RenderItem){ playerPos, 2, NULL };

        // СОРТИРУЕМ ТОЛЬКО ВИДИМОЕ (Вместо 5000 объектов сортируем ~200-300)
        qsort(renderQueue, renderQueueCount, sizeof(RenderItem), CompareY);

        // 4. РЕНДЕРИНГ
        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode2D(camera);

        // Рендерим только те тайлы земли, которые под камерой (локальный квадрат вокруг игрока)
        int startX = (int)playerPos.x - 12; if (startX < 0) startX = 0;
        int endX = (int)playerPos.x + 12;   if (endX > MAP_SIZE) endX = MAP_SIZE;
        int startY = (int)playerPos.y - 12; if (startY < 0) startY = 0;
        int endY = (int)playerPos.y + 12;   if (endY > MAP_SIZE) endY = MAP_SIZE;

        for (int y = startY; y < endY; y++) {
            for (int x = startX; x < endX; x++) {
                Vector2 isoPos = GridToIso((Vector2){ (float)x, (float)y });
                DrawTextureRec(texAtlas, recTile, (Vector2){ isoPos.x - TILE_W/2.0f, isoPos.y }, WHITE);
            }
        }

        // Рендерим отсортированные объекты
        for (int i = 0; i < renderQueueCount; i++) {
            Vector2 isoPos = GridToIso(renderQueue[i].pos);
            
            if (renderQueue[i].type == 0) { // Дерево
                DrawTextureRec(texAtlas, recTree, (Vector2){ isoPos.x - 32, isoPos.y - 86 }, WHITE);
                // Отладка хитбокса клика:
                // DrawRectangleLinesEx(((Tree*)renderQueue[i].ptr)->clickBounds, 1.0f, RED);
            } 
            else if (renderQueue[i].type == 1) { // Бот
                DrawTextureRec(texAtlas, recBot, (Vector2){ isoPos.x - 16, isoPos.y - 44 }, WHITE);
            } 
            else if (renderQueue[i].type == 2) { // Игрок
                DrawTextureRec(texAtlas, recPlayer, (Vector2){ isoPos.x - 16, isoPos.y - 44 }, WHITE);
            }
        }

        EndMode2D();

        DrawText(TextFormat("FPS: %d", GetFPS()), 10, 10, 20, BLACK);
        DrawText(TextFormat("Видимых объектов (сортировка): %d / %d", renderQueueCount, MAX_TREES + MAX_BOTS), 10, 35, 20, DARKGRAY);
        EndDrawing();
    }

	
    UnloadTexture(texAtlas);
    CloseWindow();
    return 0;
}
