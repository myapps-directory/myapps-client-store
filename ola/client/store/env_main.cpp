#include <iostream>
using namespace std;

int main(int argc, char* argv[])
{
    for (int i = 0; i < argc; ++i) {
        cout << argv[i] << endl;
    }

    cout << "PATH = " << getenv("PATH") << endl;
    cout << "QT_QPA_PLATFORM_PLUGIN_PATH = " << getenv("QT_QPA_PLATFORM_PLUGIN_PATH") << endl;
    cin.ignore();
    return 0;
}