#include "raylib.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "vector.h"

#define MAP_SIZE          1024
#define CHUNK_SIZE        16
#define CHUNKS_PER_SIDE   (MAP_SIZE / CHUNK_SIZE)

const float TILE_W = 256.0f;
const float TILE_H = 128.0f;


// ==========================================
// ИГРОВЫЕ СТРУКТУРЫ (ID И UNION БАЗИС)
// ==========================================
typedef enum { ENT_PLAYER, ENT_BOT, ENT_TREE, ENT_COUNT } EntityType;
typedef enum { TILE_TRAVA, TILE_ROMASHKA } TileState;

typedef struct {
    uint32_t imageId;
    Vector2 offset;
    float scale;   
    float speed;
    bool isLiving;
} EntityConfig;

// Очищенная сущность с юнионом
typedef struct {
    uint32_t id;         // Собственный индекс
    EntityType type;
    Vector2 pos;
    int chunkX;
    int chunkY;
    bool active;         // Флаг жизни (чтобы переиспользовать слот)

    // Специфичные данные накладываются друг на друга в памяти
    union {
        struct {
            Vector2 targetPos;
            float timer;
        } living;
        
        struct {
            uint8_t stage; // Например, стадия роста дерева
        } statica;
    } as;

} Entity;

typedef struct { TileState type; } Tile;

typedef struct {
    Tile tiles[CHUNK_SIZE][CHUNK_SIZE];
    Vector(uint32_t) entityIds; // ХРАНИМ ТОЛЬКО ID (индексы)!
} Chunk;

typedef struct {
    uint32_t id;
    Vector2 gridPos;
    Vector2 offset;
    uint32_t imageId;
    float scale;
} RenderItem;

// ==========================================
// ГЛОБАЛЬНЫЕ СИСТЕМЫ
// ==========================================
Chunk world[CHUNKS_PER_SIDE][CHUNKS_PER_SIDE];
Vector(Entity) entityPool = NULL;  // Прямой массив структур (Cash-friendly!)
Vector(RenderItem) renderQueue = NULL; 

Texture2D globalAtlas;
Rectangle atlasRects[100]; 
EntityConfig entityConfigs[ENT_COUNT];
Camera2D camera = { 0 };
uint32_t playerId = 0;

// Честная статистика для HUD
int debugCheckedChunks = 0;
int debugRenderedTiles = 0;
int debugCheckedEntities = 0;
int debugRenderedEntities = 0;

// ==========================================
// МАТЕМАТИКА
// ==========================================
Vector2 GridToIso(Vector2 gridPos) { return (Vector2){ (gridPos.x - gridPos.y) * (TILE_W / 2.0f), (gridPos.x + gridPos.y) * (TILE_H / 2.0f) }; }
Vector2 IsoToGrid(Vector2 isoPos) { return (Vector2){ (isoPos.x / (TILE_W / 2.0f) + isoPos.y / (TILE_H / 2.0f)) / 2.0f, (isoPos.y / (TILE_H / 2.0f) - isoPos.x / (TILE_W / 2.0f)) / 2.0f }; }
Rectangle GetAtlasRect(uint32_t imageId) { return atlasRects[imageId]; }

// ==========================================
// МЕНЕДЖМЕНТ СУЩНОСТЕЙ ПО ID
// ==========================================
void UpdateEntityChunkPosition(uint32_t id) {
    Entity* ent = &entityPool[id]; // Безопасное получение указателя на кадр
    
    int newX = Clamp((int)(ent->pos.x / CHUNK_SIZE), 0, CHUNKS_PER_SIDE - 1);
    int newY = Clamp((int)(ent->pos.y / CHUNK_SIZE), 0, CHUNKS_PER_SIDE - 1);

    if (ent->chunkX != newX || ent->chunkY != newY) {
        // Удаляем ID из старого чанка за O(1)
        if (ent->chunkX != -1 && ent->chunkY != -1) {
            Chunk* oldChunk = &world[ent->chunkY][ent->chunkX];
            for (size_t i = 0; i < vec_size(oldChunk->entityIds); i++) {
                if (oldChunk->entityIds[i] == id) {
                    vec_swap_remove(oldChunk->entityIds, i); 
                    break;
                }
            }
        }
        // Записываем ID в новый чанк
        ent->chunkX = newX;
        ent->chunkY = newY;
        vec_push(world[newY][newX].entityIds, id);
    }
}

uint32_t spawn_entity(EntityType type, float x, float y) {
    uint32_t id = 0;
    bool foundSlot = false;

    // 1. Ищем мертвый слот для переиспользования (чтобы индексы не съезжали)
    for (size_t i = 0; i < vec_size(entityPool); i++) {
        if (!entityPool[i].active) {
            id = i;
            foundSlot = true;
            break;
        }
    }

    // 2. Инициализируем данные
    Entity ent = {0};
    ent.id = foundSlot ? id : vec_size(entityPool);
    ent.type = type;
    ent.pos = (Vector2){ x, y };
    ent.active = true;
    ent.chunkX = -1;
    ent.chunkY = -1;

    // Специфичные данные инициализируем через юнион
    if (entityConfigs[type].isLiving) {
        ent.as.living.targetPos = ent.pos;
        ent.as.living.timer = 0;
    }

    // 3. Сохраняем в пул
    if (foundSlot) entityPool[id] = ent;
    else { vec_push(entityPool, ent); id = ent.id; }

    UpdateEntityChunkPosition(id);
    return id;
}

void InitGameData(void) {
    entityConfigs[ENT_PLAYER] = (EntityConfig){ .imageId = 1, .offset = { 436*0.5f*0.5f, 462*0.5f*0.85f }, .scale = 0.5f, .speed = 7.0f,  .isLiving = true };
    entityConfigs[ENT_BOT]    = (EntityConfig){ .imageId = 3, .offset = { 249*0.5f*0.5f, 207*0.5f*0.85f }, .scale = 0.5f, .speed = 3.5f,  .isLiving = true };
    entityConfigs[ENT_TREE]   = (EntityConfig){ .imageId = 2, .offset = { 347*0.5f*0.5f, 393*0.5f*0.9f },  .scale = 0.5f, .speed = 0.0f, .isLiving = false };
}

void LoadTextureAtlas(void) {
    globalAtlas = LoadTexture("texture.png"); 
    atlasRects[0] = (Rectangle){ 436, 393, 256, 128 }; // tile
    atlasRects[1] = (Rectangle){ 0, 0, 436, 462 };     // player
    atlasRects[2] = (Rectangle){ 436, 0, 347, 393 };   // tree 
    atlasRects[3] = (Rectangle){ 0, 462, 249, 207 };   // bot
}

int CompareY(const void* a, const void* b) {
    RenderItem* itemA = (RenderItem*)a;
    RenderItem* itemB = (RenderItem*)b;
    float ya = (itemA->gridPos.x + itemA->gridPos.y);
    float yb = (itemB->gridPos.x + itemB->gridPos.y);
    return (ya > yb) - (ya < yb);
}

// Передаем ID, а функция сама берет структуру из глобального вектора
void UpdateEntityLogic(uint32_t id, float dt) {
    Entity* ent = &entityPool[id];
    if (!ent->active) return;
    
    EntityConfig cfg = entityConfigs[ent->type];
    if (!cfg.isLiving) return;

    if (ent->type == ENT_PLAYER) {
        Vector2 moveDir = { 0, 0 };
        if (IsKeyDown(KEY_W)) { moveDir.x -= 1; moveDir.y -= 1; }
        if (IsKeyDown(KEY_S)) { moveDir.x += 1; moveDir.y += 1; }
        if (IsKeyDown(KEY_A)) { moveDir.x -= 1; moveDir.y += 1; }
        if (IsKeyDown(KEY_D)) { moveDir.x += 1; moveDir.y -= 1; }
        
        if (Vector2Length(moveDir) > 0) {
            ent->pos = Vector2Add(ent->pos, Vector2Scale(Vector2Normalize(moveDir), cfg.speed * dt));
            UpdateEntityChunkPosition(id);
        }
    } 
    else if (ent->type == ENT_BOT) {
        if (Vector2Distance(ent->pos, ent->as.living.targetPos) > 0.5f) {
            Vector2 dir = Vector2Normalize(Vector2Subtract(ent->as.living.targetPos, ent->pos));
            ent->pos = Vector2Add(ent->pos, Vector2Scale(dir, cfg.speed * dt));
            UpdateEntityChunkPosition(id);
        }
        ent->as.living.timer -= dt;
        if (ent->as.living.timer <= 0.0f) {
            ent->as.living.targetPos = (Vector2){ (float)GetRandomValue(5, MAP_SIZE - 5), (float)GetRandomValue(5, MAP_SIZE - 5) };
            ent->as.living.timer = (float)GetRandomValue(3, 8);
        }
    }
}

// ==========================================
// ГЛАВНЫЙ ЦИКЛ
// ==========================================
int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED);
    InitWindow(1280, 720, "shararam+");
    SetTargetFPS(60);

    InitGameData();
    LoadTextureAtlas();

    for (int cy = 0; cy < CHUNKS_PER_SIDE; cy++) {
        for (int cx = 0; cx < CHUNKS_PER_SIDE; cx++) world[cy][cx].entityIds = NULL;
    }

    playerId = spawn_entity(ENT_PLAYER, MAP_SIZE / 2.0f, MAP_SIZE / 2.0f);
    
    for (int i = 0; i < 3000; i++) spawn_entity(ENT_TREE, (float)GetRandomValue(20, MAP_SIZE - 20), (float)GetRandomValue(20, MAP_SIZE - 20));
    for (int i = 0; i < 500; i++)  spawn_entity(ENT_BOT, (float)GetRandomValue(20, MAP_SIZE - 20), (float)GetRandomValue(20, MAP_SIZE - 20));

    camera.zoom = 1.0f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        camera.offset = (Vector2){ GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f };
        if (entityPool[playerId].active) camera.target = GridToIso(entityPool[playerId].pos);

        float wheel = GetMouseWheelMove();
        if (wheel != 0) camera.zoom = Clamp(camera.zoom + wheel * 0.1f, 0.1f, 3.0f);

        // 1. ГРУБЫЙ КУЛЛИНГ ЧАНКОВ
        Vector2 screenCorners[4] = {
            GetScreenToWorld2D((Vector2){ 0, 0 }, camera), GetScreenToWorld2D((Vector2){ (float)GetScreenWidth(), 0 }, camera),
            GetScreenToWorld2D((Vector2){ 0, (float)GetScreenHeight() }, camera), GetScreenToWorld2D((Vector2){ (float)GetScreenWidth(), (float)GetScreenHeight() }, camera)
        };

        float minGridX = MAP_SIZE, maxGridX = 0, minGridY = MAP_SIZE, maxGridY = 0;
        for (int i = 0; i < 4; i++) {
            Vector2 g = IsoToGrid(screenCorners[i]);
            if (g.x < minGridX) minGridX = g.x; if (g.x > maxGridX) maxGridX = g.x;
            if (g.y < minGridY) minGridY = g.y; if (g.y > maxGridY) maxGridY = g.y;
        }

        int startChunkX = Clamp(((int)minGridX / CHUNK_SIZE) - 1, 0, CHUNKS_PER_SIDE - 1);
        int endChunkX   = Clamp(((int)maxGridX / CHUNK_SIZE) + 1, 0, CHUNKS_PER_SIDE - 1);
        int startChunkY = Clamp(((int)minGridY / CHUNK_SIZE) - 1, 0, CHUNKS_PER_SIDE - 1);
        int endChunkY   = Clamp(((int)maxGridY / CHUNK_SIZE) + 1, 0, CHUNKS_PER_SIDE - 1);

        vec_clear(renderQueue);
        debugCheckedChunks = 0;
        debugCheckedEntities = 0;

        int scrW = GetScreenWidth(), scrH = GetScreenHeight();

        // 2. ОБНОВЛЕНИЕ ЛОГИКИ И ТОЧНЫЙ КУЛЛИНГ СУЩНОСТЕЙ
        for (int cy = startChunkY; cy <= endChunkY; cy++) {
            for (int cx = startChunkX; cx <= endChunkX; cx++) {
                debugCheckedChunks++;
                Chunk* chunk = &world[cy][cx];
                
                for (size_t i = 0; i < vec_size(chunk->entityIds); i++) {
                    uint32_t id = chunk->entityIds[i];
                    debugCheckedEntities++;
                    
                    UpdateEntityLogic(id, dt);
                    Entity* ent = &entityPool[id];

                    Vector2 scrPos = GetWorldToScreen2D(GridToIso(ent->pos), camera);
                    float margin = 250.0f;
                    
                    // Жесткая проверка: сущность физически на мониторе?
                    if (scrPos.x >= -margin && scrPos.x <= scrW + margin && scrPos.y >= -margin && scrPos.y <= scrH + margin) {
                        EntityConfig cfg = entityConfigs[ent->type];
                        RenderItem item = { .id = id, .gridPos = ent->pos, .offset = cfg.offset, .imageId = cfg.imageId, .scale = cfg.scale };
                        vec_push(renderQueue, item); 
                    }
                }
            }
        }

        debugRenderedEntities = (int)vec_size(renderQueue);
        qsort(renderQueue, vec_size(renderQueue), sizeof(RenderItem), CompareY);

        // 3. ОТРИСОВКА
        BeginDrawing();
        ClearBackground(BLACK); 
        BeginMode2D(camera);

        debugRenderedTiles = 0;
        Rectangle groundSrc = GetAtlasRect(0);
        for (int cy = startChunkY; cy <= endChunkY; cy++) {
            for (int cx = startChunkX; cx <= endChunkX; cx++) {
                for (int y = 0; y < CHUNK_SIZE; y++) {
                    for (int x = 0; x < CHUNK_SIZE; x++) {
                        float globalX = (float)(cx * CHUNK_SIZE + x);
                        float globalY = (float)(cy * CHUNK_SIZE + y);
                        
                        Vector2 isoPos = GridToIso((Vector2){ globalX, globalY });
                        Vector2 scrPos = GetWorldToScreen2D(isoPos, camera);

                        if (scrPos.x >= -TILE_W && scrPos.x <= scrW + TILE_W && scrPos.y >= -TILE_H && scrPos.y <= scrH + TILE_H) {
                            DrawTextureRec(globalAtlas, groundSrc, (Vector2){isoPos.x - TILE_W/2.0f, isoPos.y}, WHITE);
                            debugRenderedTiles++; 
                        }
                    }
                }
            }
        }

        for (size_t i = 0; i < vec_size(renderQueue); i++) {
            Vector2 isoPos = GridToIso(renderQueue[i].gridPos);
            Vector2 drawPos = Vector2Subtract(isoPos, renderQueue[i].offset);
            Rectangle srcRect = GetAtlasRect(renderQueue[i].imageId);
            Rectangle destRect = { drawPos.x, drawPos.y, srcRect.width * renderQueue[i].scale, srcRect.height * renderQueue[i].scale };
            DrawTexturePro(globalAtlas, srcRect, destRect, (Vector2){0, 0}, 0.0f, WHITE);
        }

        EndMode2D();

        // 4. ЧЕСТНАЯ АНАЛИТИКА В HUD
        DrawRectangle(5, 5, 450, 140, Fade(BLACK, 0.85f));
        DrawText(TextFormat("FPS: %d", GetFPS()), 15, 10, 20, LIME);
        DrawText(TextFormat("Chunks Evaluated: %d", debugCheckedChunks), 15, 35, 20, WHITE);
        DrawText(TextFormat("Tiles Rendered (on screen): %d", debugRenderedTiles), 15, 60, 20, LIGHTGRAY);
        DrawText(TextFormat("Entities Checked (in chunks): %d", debugCheckedEntities), 15, 85, 20, ORANGE);
        DrawText(TextFormat("Entities Rendered (on screen): %d / %d Total", debugRenderedEntities, (int)vec_size(entityPool)), 15, 110, 20, YELLOW);
        
        EndDrawing();
    }

    // Очистка
    for (int cy = 0; cy < CHUNKS_PER_SIDE; cy++) {
        for (int cx = 0; cx < CHUNKS_PER_SIDE; cx++) vec_free(world[cy][cx].entityIds);
    }
    vec_free(entityPool);
    vec_free(renderQueue);

    UnloadTexture(globalAtlas);
    CloseWindow();
    return 0;
}
