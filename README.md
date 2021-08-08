
# parTTYcles

## Clone

```
git clone 
git submodule init
git submodule update
```

## Build

```
make clean
make
```

## Run-time Preparation

Run a broker with a corresponding message generator.
You can use, e.g., [bowerick](https://github.com/ruedigergad/bowerick) (download from dist/) and run it as follows:

```
java -jar dist/bowerick-2.8.0-standalone.jar -G yin-yang -I 40 -u '[tcp://127.0.0.1:1031 mqtt://127.0.0.1:1701 ws://127.0.0.1:1864 stomp://127.0.0.1:2000]'
```

## Run

```
./parTTYcles
```

