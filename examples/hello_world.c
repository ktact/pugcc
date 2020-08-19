// How to run:
//
// $ make
// $ ./pugcc examples/hello_world.c > tmp.s
// $ gcc -static -o tmp tmp.s
// $ ./tmp

int main() {
    printf("Hello, world\n");

    return 0;
}
