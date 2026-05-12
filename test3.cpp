#include <iostream>
using namespace std;

struct Node {
    int data;
    Node* next;
};

int main() {
    Node* head = NULL;

    // 🔹 Insert at beginning (10)
    Node* newNode1 = new Node();
    newNode1->data = 10;
    newNode1->next = head;
    head = newNode1;

    // 🔹 Insert at end (20)
    Node* newNode2 = new Node();
    newNode2->data = 20;
    newNode2->next = NULL;

    if (head == NULL) {
        head = newNode2;
    } else {
        Node* temp = head;
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = newNode2;
    }

    // 🔹 Insert at end (30)
    Node* newNode3 = new Node();
    newNode3->data = 30;
    newNode3->next = NULL;

    Node* temp = head;
    while (temp->next != NULL) {
        temp = temp->next;
    }
    temp->next = newNode3;

    // 🔹 Insert at position 2 (value = 15)
    int pos = 2;
    Node* newNode4 = new Node();
    newNode4->data = 15;

    if (pos == 1) {
        newNode4->next = head;
        head = newNode4;
    } else {
        Node* temp2 = head;
        for (int i = 1; i < pos - 1 && temp2 != NULL; i++) {
            temp2 = temp2->next;
        }

        if (temp2 == NULL) {
            cout << "Position out of range\n";
        } else {
            newNode4->next = temp2->next;
            temp2->next = newNode4;
        }
    }

    // 🔹 Print Linked List
    cout << "Linked List: ";
    Node* temp3 = head;
    while (temp3 != NULL) {
        cout << temp3->data << " -> ";
        temp3 = temp3->next;
    }
    cout << "NULL\n";

    return 0;
}