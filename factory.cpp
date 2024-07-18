#include<bits/stdc++.h>
using namespace std;

class product{
    public:
    virtual string operation() = 0;
};

class ByProduct1: public product{
    public:
    string operation() override{
        cout<<"This is for performing operation for ByProduct 1.\n";
    }
};

class ByProduct2: public product{
    public:
    string operation() override{
        cout<<"This is for performing operation for ByProduct 2.\n";
    }
};

class creator{
    public:
    product* pro = NULL;

    virtual product* assign() = 0;

    void operate(){
        pro=this->assign();

        pro->operation();
    }

    

};

class SubCreator1: public creator{
    public:
    product* assign() override{

        return new ByProduct1();
    }
};

class SubCreator2: public creator{
    public:
    product* assign() override{
        return new ByProduct2();
    }
};

void result(creator* create){
    
        
        create->operate();
    }

int main(){
    creator* create = new SubCreator2();
    result(create);
}

