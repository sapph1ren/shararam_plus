#include "player.hpp"
#include "World.hpp"
#include "Anim.hpp"
#include "objects.hpp"
#include "resourceMan.hpp"
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <functional>
using UseCallback = std::function<void(Obj* caller)>;
struct Player {
	int pers; // 0 - крош, 9 - совунья
	sf::Sprite sprite;
	float speed;
	sf::Vector2f pos;
	float hung; //голод 0-1
	int Temp;
};
struct Obj { 
	std::string type;
	int mass;
	float prochn;
	sf::Sprite sprite;
	std::vector<int> kto_mozh_use;
	UseCallback onUse = nullptr;
};
struct Tail {
	sf::VertexArray vertices;
	int biom; // 0 - луг, 1 - лес, 2 - болото, 3- пляж, 4 - заросли
	sf::FloatRect kraya;
	int tid;
};
struct WORLD {
	int DayP; //0- утро, 1 - день, 2 - вечер, 3 - ночь
	bool rain;
	bool snow;
	int Temp; //температура воздуха в мире "по-дефолту", а биомах типа леса отдельно расчитывается 
	int VrG; //время года, 0 - лето, 3 - весна


};
