# README

## 目的
ソケットプログラミングについて学ぶために，自作 DHCP を作成した．

## 機能
- DHCP サーバは DHCP クライアントへ IP アドレスを割り当てる．
- DHCP サーバは同時に複数のクライアントの要求を受け付けることができる．

## 通信について
- 通信には UDP を使用する．
- サーバとクライアントは IP アドレスの割り当てや解放の際に，パケットに搭載するメッセージを通して割り当て成功の可否などを通信する．
- メッセージの種類は以下の通りである．

1. DISCOVER: クライアント -> サーバ
- クライアントは IP アドレスを取得するために，これを割り当てることのできるサーバを探す．
この時 DHCP DISCOVER メッセージをサーバへ送信する．
本来であれば複数のサーバへマルチキャストでメッセージを送信するが，今回は簡単のためサーバは一つとする．

2. OFFER: サーバ -> クライアント
- サーバが，クライアントに対して割り当て可能な IP アドレスが存在するかどうかを提示するためのメッセージである．

3. REQUEST: クライアント -> サーバ
- クライアントがサーバへ IP アドレスの割り当てを要求するメッセージである．
- また，クライアントは IP アドレスの使用期限の延長を要求する際も，このメッセージを用いる．

4. ACK: サーバ -> クライアント
- サーバが，クライアントの REQUEST メッセージを受け取った際に，承認の有無をクライアントへ示すメッセージである．

5. RELEASE: クライアント -> サーバ
- クライアントが IP アドレスの解放のためにサーバへ送るメッセージである．サーバはこれを受け取ると IP アドレスを解放する．

## 通信の流れ
通信の流れを以下に示す．
1. クライアントはサーバへ DHCP DISCOVER メッセージを送り，IP アドレスの割り当てを要求する．
2. サーバはクライアントから DHCP DISCOVER メッセージを受け取ると，割り当て可能な IP アドレスが存在するかを確認する．
その後クライアントへ DHCP OFFER メッセージを送る．
3. クライアントは DHCP OFFER メッセージを受け取ると，サーバへ DHCP REQUEST メッセージを送信し，割り当て要求を送る．
4. サーバは　DHCP REQUEEST メッセージを受け取ると，クライアントへ 該当の IP アドレスを割り当て DHCP ACK メッセージを返す．
5. クライアントは割り当てられた IP アドレスの使用期限がくると，サーバへ IP アドレスの延長要求の送信の有無を決める．
もし延長を要求するのであれば，DHCP REQUEST メッセージを，そうでなければ DHCP RELEASE メッセージを送る．
6. サーバは DHCP REQUEST にてクライアントからの IP アドレス延長要求を受け取ると，該当する IP アドレスの使用期限を延長し，DHCP ACK メッセージをクライアントへ返す．

## プログラムの仕様
1. client
 - 起動したらサーバに IP アドレスの割り当てを要求する.
 - server 側の都合で割り当てが不可能である場合は実行を終了する.
 - メッセージを送信した際は、その内容と宛先の IP アドレスを表示する.
 - メッセージを受信した際は、その内容と送り主の IP アドレスを表示する.
 - IP アドレスとネットマスクを取得すると、その値と使用期限を表示する.
 - 使用期限の 1/2 が経過すると、ユーザに IP アドレスを延長するかを確認する.（標準入力を待つ）
 - ユーザが延長を希望すれば、再度 REQUEST メッセージを送信し、使用期限を延長する.
 - ユーザが延長を希望しなかった場合や何も入力しなかった場合は RELEASE メッセージを送信し、実行を終了する.
 - メッセージの受信待ちでタイムアウトをした際には RELEASE メッセージを送信し、実行を終了する.（タイムアウトは 10 秒）
 - プロトコルと整合しないメッセージを受信した場合、RELEASE メッセージを送信し、実行を終了する.
 - SIGHUP を受信すると RELEASE メッセージを送信し、実行を終了する.

2. server
- 起動したら設定ファイルを読み込み、使用期限、及び使用可能な IP アドレスとネットマスクの組みを双方向リストに格納し、出力する.
- IP アドレスとネットマスクを割り当てる際は最も使われていないものから割り当てる.
- メッセージを送信した際は、その内容と宛先の IP アドレス、及び割り当てた IP アドレスを表示する.
- メッセージを受信した際は、その内容と送り主の IP アドレス、及び割り当てた IP アドレスを表示する.
- IP アドレスとネットマスクを割り当てた際は、クライアントの IP アドレス、割り当てた　IP アドレスとネットマスクの値と利用期限を表示する.
- IP アドレスの使用期限が切れた場合はその旨を表示し、割り当てた　IP アドレスとネットマスクを回収する.
- REQUEST メッセージを受信すると、クライアントが使用していた IP アドレスを回収し、そのクライアントとの処理を終了する.
- プロトコルと整合しないメッセージを受信した場合、該当するクライアントとの処理を終了する.

## 苦労した点
- 一番初めのコネクションの確立が中々上手くいかず非常に苦労した．
- 解決のために，一度コードから離れ，机上で行うべきことを整理した．(状態遷移図の活用など)
- またシェルの開発の際と同様，行うべきことを細かく分け，段階的にバグを解決することを心がけた．

## 振り返り
- 擬似 DHCP ということで，今回は実際の DHCP とは異なりサーバの数は一つとしている．
これに伴い，クライアントがサーバを探す際も本来は複数のサーバへマルチキャストでメッセージを送信するが，今回はこれを行っていない．
- 今回は状態遷移図を記述してからコーディングを行なった．
シェルを実装した際も状態遷移図を書いたが，予め処理を図に書くことで頭の中を整理することができ，より良いコードを書くことができたと感じている．
今後もコードの質を向上させるために，状態遷移図を活用していきたい．
