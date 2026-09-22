const list = new ImageList();
let loaded = false;
let failed = false;
let loadedImage;

const image = list.load("tests/my_image.png", {
    onLoad: (result) => {
        loaded = true;
        loadedImage = result;
    },
    onError: () => {
        failed = true;
    },
});

if (!image.loading() || image.ready()) {
    throw new Error("ImageList image was not queued");
}

while (list.pending() > 0) {
    if (list.process() !== 1) {
        throw new Error("ImageList.process did not process one image");
    }
}

if (failed) {
    throw new Error("ImageList reported an unexpected load failure");
}
if (!loaded || loadedImage !== image) {
    throw new Error("ImageList onLoad callback was not dispatched");
}
if (!image.ready()) {
    throw new Error("ImageList onLoad image is not ready");
}

let errorImage;
let errorPath;
const missing = list.load("host:/missing-image.png", {
    onError: (result, path) => {
        errorImage = result;
        errorPath = path;
    },
});

list.process();
if (!missing.failed() || missing.ready() ||
    errorImage !== missing || errorPath !== "host:/missing-image.png") {
    throw new Error("ImageList onError callback was not dispatched correctly");
}

image.lock();
while (true) {
    Screen.clear(0x80182030);
    image.draw(
        (Screen.getMode().width - image.width) / 2,
        (Screen.getMode().height - image.height) / 2
    );
    Screen.flip();
}
