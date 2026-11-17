# Slime (Geode · GD 2.2081 · Android ARM64)

Bloques que rebotan como slime (por grupo, sin tocar el editor) y un estilo visual slime. Compatible con Geode 5.11.2.

## Cómo sacar el .geode (sin PC)

1. En github.com (modo "sitio de escritorio" si estás en el celu) creá un repositorio nuevo, distinto al de Spam Buff.
2. **Add file → Upload files** y subí: `mod.json`, `CMakeLists.txt`, `main.cpp`, `SlimeLogic.hpp`, `about.md`, `changelog.md`, `logo.png`. Commit.
3. **Add file → Create new file**, nombre `.github/workflows/build.yml` y pegá el contenido de `build.yml`. Commit.
4. Pestaña **Actions** → se ejecuta sola. Tarda unos 5–10 min.
5. Entrá a la ejecución en verde → **Artifacts** → bajá `Slime-geode`. Adentro está `lleomc2.slime.geode`.

Si falla la compilación, copiá el error del paso "Compilar el mod" y mandámelo.

## Cómo funciona

- Rebote: engancha `PlayerObject::hitGround` (detecta el aterrizaje sobre un objeto del grupo) y aplica la nueva velocidad vertical en `postCollision`. La velocidad de GD es positiva hacia arriba en el mundo, por eso el mismo cálculo sirve con gravedad invertida. La lógica está en `SlimeLogic.hpp` (probada en PC).
- Textura: un shader puesto en las capas de bloques y decoración. Parte del color original de cada pixel (conserva el color de cada bloque) y le agrega material de slime: relieve viscoso procedural, borde redondeado, brillo húmedo, reflejo de cielo y burbujas. Es solo visual. Se crea un programa por tamaño de textura y, si Android pierde el contexto de OpenGL, se recrea solo.

Si el rebote no se nota, bajá "Velocidad mínima de caída"; si sale muy alto, bajá "Fuerza del rebote" o "Rebote máximo".
