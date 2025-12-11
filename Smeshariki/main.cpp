#include <SFML/Graphics.hpp>
#include <vector>
#include <algorithm>
#include <memory>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <map>
#include "main.hpp"


int main() {
    sf::RenderWindow window(sf::VideoMode(800, 600), "CMEIIIAPNKN");
    sf::Font font;
    if (!font.loadFromFile("C:/Windows/Fonts/arial.ttf"))
    {
        std::cerr << "Ошибка при загрузке шрифта! Проверьте путь к файлу arial.ttf" << std::endl;
        return -1; 
    }
    sf::Text text;
    text.setFont(font); 
    text.setString("Смешарики");
    text.setCharacterSize(48);
    text.setFillColor(sf::Color::White);
    sf::FloatRect textRect = text.getLocalBounds();
    text.setOrigin(textRect.left + textRect.width / 2.0f, textRect.top + textRect.height / 2.0f);
    text.setPosition(sf::Vector2f(window.getSize().x / 2.0f, window.getSize().y / 2.0f));
    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) if (event.type == sf::Event::Closed) window.close();
        window.clear(sf::Color::Black);
        window.draw(text);
        window.display();
    }
	return 0;
}