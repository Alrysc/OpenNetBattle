﻿#pragma once
#include <Swoosh\Segue.h>
#include <Swoosh\Ease.h>
#include <Swoosh\EmbedGLSL.h>

using namespace swoosh;

namespace {
  // Modified by TheMaverickProgrammer slightly to support GLSL 1.10
  auto CROSSZOOM_FRAG_SHADER = GLSL
  (
    110,
    uniform sampler2D texture;
    uniform sampler2D texture2;
    uniform float progress;

    // License: MIT
    // Author: rectalogic
    // ported by gre from https://gist.github.com/rectalogic/b86b90161503a0023231

    // Converted from https://github.com/rectalogic/rendermix-basic-effects/blob/master/assets/com/rendermix/CrossZoom/CrossZoom.frag
    // Which is based on https://github.com/evanw/glfx.js/blob/master/src/filters/blur/zoomblur.js
    // With additional easing functions from https://github.com/rectalogic/rendermix-basic-effects/blob/master/assets/com/rendermix/Easing/Easing.glsllib

    uniform float strength;

    const float PI = 3.141592653589793;

    float Linear_ease(in float begin, in float change, in float duration, in float time) {
      return change * time / duration + begin;
    }

    float Exponential_easeInOut(in float begin, in float change, in float duration, in float time) {
      if (time == 0.0)
        return begin;
      else if (time == duration)
        return begin + change;
      time = time / (duration / 2.0);
      if (time < 1.0)
        return change / 2.0 * pow(2.0, 10.0 * (time - 1.0)) + begin;
      return change / 2.0 * (-pow(2.0, -10.0 * (time - 1.0)) + 2.0) + begin;
    }

    float Sinusoidal_easeInOut(in float begin, in float change, in float duration, in float time) {
      return -change / 2.0 * (cos(PI * time / duration) - 1.0) + begin;
    }

    float rand(vec2 co) {
      return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
    }

    vec3 crossFade(in vec2 uv, in float dissolve) {
      return mix(texture2D(texture, uv).rgb, texture2D(texture2, uv).rgb, dissolve);
    }

    void main() {
      vec2 uv = gl_TexCoord[0].xy;

      vec2 texCoord = uv.xy / vec2(1.0).xy;

      // Linear interpolate center across center half of the image
      vec2 center = vec2(Linear_ease(0.25, 0.5, 1.0, progress), 0.5);
      float dissolve = Exponential_easeInOut(0.0, 1.0, 1.0, progress);

      // Mirrored sinusoidal loop. 0->strength then strength->0
      float strength = Sinusoidal_easeInOut(0.0, strength, 0.5, progress);

      vec3 color = vec3(0.0);
      float total = 0.0;
      vec2 toCenter = center - texCoord;

      /* randomize the lookup values to hide the fixed number of samples */
      float offset = rand(uv);

      for (float t = 0.0; t <= 40.0; t++) {
        float percent = (t + offset) / 40.0;
        float weight = 4.0 * (percent - percent * percent);
        color += crossFade(texCoord + toCenter * percent * strength, dissolve) * weight;
        total += weight;
      }
      
      gl_FragColor = vec4(color / total, 1.0);
    }

   );
}

template<int percent_power> // divided by 100 to yeild %
class CrossZoomCustom : public Segue {
private:
  sf::Texture* temp;
  sf::Shader shader;

public:
  virtual void onDraw(sf::RenderTexture& surface) {
    double elapsed = getElapsed().asMilliseconds();
    double duration = getDuration().asMilliseconds();
    double alpha = ease::linear(elapsed, duration, 1.0);

    this->drawLastActivity(surface);

    surface.display(); // flip and ready the buffer

    if (temp) delete temp;
    temp = new sf::Texture(surface.getTexture()); // Make a copy of the source texture

    sf::Sprite sprite(*temp);

    surface.clear(sf::Color::Transparent);
    this->drawNextActivity(surface);

    surface.display(); // flip and ready the buffer

    sf::Texture* temp2 = new sf::Texture(surface.getTexture()); // Make a copy of the source texture

    shader.setUniform("progress", (float)alpha);
    shader.setUniform("texture2", *temp2);
    shader.setUniform("texture", *temp);

    sf::RenderStates states;
    states.shader = &shader;

    surface.draw(sprite, states);
  }

  CrossZoomCustom(sf::Time duration, Activity* last, Activity* next) : Segue(duration, last, next) {
    /* ... */
    temp = nullptr;

    shader.loadFromMemory(::CROSSZOOM_FRAG_SHADER, sf::Shader::Fragment);
    shader.setUniform("strength", (float)percent_power/100.0f);

  }

  virtual ~CrossZoomCustom() { ; }
};

using CrossZoom = CrossZoomCustom<40>;