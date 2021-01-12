//
// Created by morgan on 12/01/21.
//
#include <iostream>
#include "system_error"

using namespace std;

int main(int argc, char **argv) {
    error_code *ec;
    ec = new error_code(0, system_category());
    cout << ec->value() << endl;

    if (ec->value())
        cout<<"true"<<endl;
    else
        cout<<"false"<<endl;
    return 0;


}

