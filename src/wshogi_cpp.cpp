/**
 * @file wshogi_cpp.cpp
 * @brief ���@��̐�����������dll����������
 * @author SUEYOSHI Ryosuke
 * @date 2023-07-10
 * 
 * �����G���W���ushogi686micro�v�̃\�[�X�R�[�h������
 * ���@��̐�������������������dll���������́B
 * 
 * merom686/shogi686micro: �\�[�X�t�@�C��1�ŏ����̎v�l�G���W��
 * https://github.com/merom686/shogi686micro
 * 
 * dll�����̃R���p�C���R�}���h�͈ȉ��B
 * --
 * g++ -O2 -mtune=generic -s -shared -o shogimicro_cpp.dll shogimicro_cpp.cpp
 * --
 */

#include <iostream>
#include <regex>
#include <chrono>

//#define _assert(x) ((void)0)
#define _assert(x) \
if (!(x)){ std::cout << "info string error file:" << __FILE__ << " line:" << __LINE__ << std::endl; throw; }

enum Piece_Turn {
	King, Rook, Bishop, Gold, Silver, Knight, Lance, Pawn,
	HandTypeNum, Dragon, Horse, ProSilver = 12, ProKnight, ProLance, ProPawn,
	PieceTypeNum, BlackMask = PieceTypeNum, WhiteMask = BlackMask << 1,
};

enum Color {
	Black, White, ColorNum
};

const int MaxMove = 593, MaxPly = 32, MaxGamePly = 1024;
const std::string sfenPiece = "KRBGSNLPkrbgsnlp";

const short ScoreInfinite = INT16_MAX;
const short ScoreMatedInMaxPly = ScoreInfinite - MaxPly;

volatile bool stop;
uint64_t nodes;

struct Stack;

//�w����
class Move
{
	int value;

public:
	static const int MoveNone = 0;

	int from() const {
		return value & 0xff;
	}
	int to() const {
		return value >> 8 & 0xff;
	}
	int piece() const {
		return value >> 16 & 0xf;
	}
	int promote() const {
		return value >> 20 & 0x1;
	}
	int cap() const {
		return value >> 21 & 0xf;
	}
	//�ړ���̋�
	int pieceTo() const {
		return piece() | promote() << 3;
	}
	bool isNone() const {
		return (value == MoveNone);
	}
	//USI�`���ɕϊ�
	std::string toUSI() const;
	//�ړ���(���̂Ƃ���0)8bit, �ړ���8bit, �ړ��u�O�v�̋�4bit, ��������1bit, �������4bit
	Move(int from, int to, int piece, int promote, int cap){
		value = from | to << 8 | piece << 16 | promote << 20 | cap << 21;
	}
	Move(int v) : value(v){}
	Move(){}
};

//�ǖ�
struct Position
{
	static const int FileNum = 9, RankNum = 9, PromotionRank = 3;
	static const int Stride = FileNum + 1, Origin = Stride * 3, SquareNum = Origin + Stride * (RankNum + 2);

	//piece_turn: ��̎��3bit, ��1bit, ���̋�1bit, ���̋�1bit �ȏ�6bit;�ǂ͑S8bit�������Ă���
	uint8_t piece_turn[SquareNum], hand[ColorNum][HandTypeNum], turn;
	uint8_t king[ColorNum];//�ʂ̈ʒu
	uint8_t continuousCheck[ColorNum];//���݂̘A�������

	//�T�����n�߂������ƏI�����鎞��
	std::chrono::system_clock::time_point timeStart, timeEnd;
	int ply, gamePly;//Root����̎萔�A�J�n�ǖʂ���̎萔
	Stack *ss;//Root��Stack�ʒu

	//�ǖʂ��r���� ����Ȃ�0��Ԃ�
	static int compare(const Position &p1, const Position &p2){
		bool bp = std::equal(p1.piece_turn + Origin, p1.piece_turn + Origin + Stride * RankNum, p2.piece_turn + Origin);
		bool bh = std::equal(p1.hand[Black], p1.hand[White], p2.hand[Black]);//���̎��������r����΂悢
		bool bt = (p1.turn == p2.turn);
		return (bp && bh && bt) ? 0 : 1;
	}
	//square(0, 0)�͔Ղ̍������\��(�E����ł͂Ȃ�)
	static int square(int x, int y){
		return Origin + Stride * y + x;
	}
	static int turnMask(int turn){
		return (turn == Black) ? BlackMask : WhiteMask;
	}

	int turnMask() const {
		return turnMask(turn);
	}
	//������ԂɂƂ��ēG�wrank�i�ڂ܂łɂ��邩
	template <int rank = PromotionRank>
	bool promotionZone(int sq) const {
		if (turn == Black){
			return sq < square(0, rank);
		} else {
			return sq >= square(0, RankNum - rank);
		}
	}
	//turn�̋ʂɉ��肪�������Ă��邩
	bool inCheck(const int turn) const;
	//���i�߂�
	void doMove(Stack *const ss, const Move move);
	//������u(���E��)�łȂ����m���߂�
	bool isLegal(Stack *const ss, const Move pseudoLegalMove);
	void clear(){
		std::memset(this, 0, sizeof *this);
		std::fill_n(piece_turn, SquareNum, 0xff);//�ǂŖ��߂�
		for (int y = 0; y < RankNum; y++){
			std::fill_n(&piece_turn[square(0, y)], FileNum, 0);//y�i�ڂ�S���󂫏���
		}
	}
};

struct Stack
{
	Move pv[MaxPly];//�ǂ݋؂��L�^����
	Move currentMove;//���ܓǂ�ł����
	bool checked;//��Ԃ̋ʂɉ��肪�������Ă��邩
	Position pos;//�ǖʂ�ۑ����āA����茟�o����߂��̂Ɏg��
};

//�w�肳�ꂽ��̗���������S�Ă̏��ɑ΂��āAtrue��Ԃ��܂�f�����s����
template <class F>
inline bool forAttack(const uint8_t *pt, const int sq, const int p, const int turn, F f)
{
	static const int8_t n = Position::Stride;
	static const int8_t att[PieceTypeNum][10] = {
		{ -n - 1, -n, -n + 1, -1, 1, n - 1, n, n + 1, 0, 0 },//��
		{ 0, -n, -1, 1, n, 0, 0, 0, 0, 0 },
		{ 0, -n - 1, -n + 1, n - 1, n + 1, 0, 0, 0, 0, 0 },
		{ -n - 1, -n, -n + 1, -1, 1, n, 0, 0, 0, 0 },
		{ -n - 1, -n, -n + 1, n - 1, n + 1, 0, 0, 0, 0, 0 },
		{ -n * 2 + 1, -n * 2 - 1, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0, -n, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ -n, 0, 0, 0, 0, 0, 0, 0, 0, 0 },//��
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ -n - 1, -n + 1, n - 1, n + 1, 0, -n, -1, 1, n, 0 },//��
		{ -n, -1, 1, n, 0, -n - 1, -n + 1, n - 1, n + 1, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
		{ -n - 1, -n, -n + 1, -1, 1, n, 0, 0, 0, 0 },
		{ -n - 1, -n, -n + 1, -1, 1, n, 0, 0, 0, 0 },
		{ -n - 1, -n, -n + 1, -1, 1, n, 0, 0, 0, 0 },
		{ -n - 1, -n, -n + 1, -1, 1, n, 0, 0, 0, 0 },
	};

	const int sgn = (turn == Black) ? 1 : -1;
	const int8_t *a = att[p];
	int i;
	for (i = 0; a[i] != 0; i++){
		if (f(sq + a[i] * sgn)) return true;
	}
	for (i++; a[i] != 0; i++){
		for (int d = a[i];; d += a[i]){
			if (f(sq + d * sgn)) return true;
			if (pt[sq + d * sgn] != 0) break;
		}
	}
	return false;
}

std::string Move::toUSI() const
{
	std::string s;
	auto add = [&](int sq){
		sq -= Position::Origin;
		s += '1' + Position::FileNum - 1 - sq % Position::Stride;
		s += 'a' + sq / Position::Stride;
	};

	if (from() == 0){
		s += sfenPiece[piece()];
		s += '*';
		add(to());
	} else {
		add(from());
		add(to());
		if (promote()) s += '+';
	}
	return s;
}

inline bool Position::inCheck(const int turn) const
{
	for (int p = King; p < PieceTypeNum; p++){
		const int pt = p | Position::turnMask(turn ^ 1);
		bool ret = forAttack(piece_turn, king[turn], p, turn, [&](int sq){
			return piece_turn[sq] == pt;
		});
		if (ret) return true;
	}
	return false;
}

inline void Position::doMove(Stack *const ss, const Move move)
{
	if (move.from() == 0){
		//�ł�
		hand[turn][move.piece()]--;
		piece_turn[move.to()] = move.piece() | turnMask();
	} else {
		//�ړ�
		if (move.cap()){
			//���
			hand[turn][move.cap() % HandTypeNum]++;
		}
		piece_turn[move.from()] = 0;
		piece_turn[move.to()] = move.pieceTo() | turnMask();
		if (move.piece() == King) king[turn] = move.to();
	}
	turn ^= 1;
	ply++;
	gamePly++;

	//���܎w������
	ss->currentMove = move;
	//���܎w�����肪���肾������
	(ss + 1)->checked = inCheck(turn);
	//�A������̉񐔂��X�V
	if ((ss + 1)->checked){
		continuousCheck[ss->pos.turn]++;
	} else {
		continuousCheck[ss->pos.turn] = 0;
	}
}

inline bool Position::isLegal(Stack *const ss, const Move pseudoLegalMove)
{
	doMove(ss, pseudoLegalMove);
	bool illegal = inCheck(turn ^ 1);
	*this = ss->pos;//���߂�
	return !illegal;
}

//�S�Ă̍��@��(������u���܂�)�𐶐����A���������w����̌���Ԃ�
int generateMoves(Move *const moves, const Position &pos)
{
	Move *m = moves;
	int pawn = 0;//������o�p�̃r�b�g�}�b�v
	const int t = pos.turnMask();
	//�ړ�
	for (int y = 0; y < Position::RankNum; y++){
		for (int x = 0; x < Position::FileNum; x++){
			int from = Position::square(x, y);
			int pt = pos.piece_turn[from];
			if (pt & t){
				int p = pt % PieceTypeNum;
				if (p == Pawn) pawn |= 1 << x;
				forAttack(pos.piece_turn, from, p, pos.turn, [&](int to){
					int cap = pos.piece_turn[to];
					if (!(cap & t)){//�����̋�ƕǈȊO(�󏡂Ƒ���̋�)�ւȂ�ړ��ł���
						if (p < HandTypeNum && p != King && p != Gold
							&& (pos.promotionZone(from) || pos.promotionZone(to))){
							*m++ = Move{ from, to, p, 1, cap % PieceTypeNum };
						}
						if (!((p == Pawn || p == Lance) && pos.promotionZone<1>(to))
							&& !(p == Knight && pos.promotionZone<2>(to))){
							*m++ = Move{ from, to, p, 0, cap % PieceTypeNum };
						}
					}
					return false;
				});
			}
		}
	}
	//�ł�
	for (int p = Rook; p < HandTypeNum; p++){
		if (!pos.hand[pos.turn][p]) continue;
		for (int y = 0; y < Position::RankNum; y++){
			for (int x = 0; x < Position::FileNum; x++){
				int to = Position::square(x, y);
				int pt = pos.piece_turn[to];
				if (pt == 0 && !(p == Pawn && (pawn & 1 << x))){
					if (!((p == Pawn || p == Lance) && pos.promotionZone<1>(to))
						&& !(p == Knight && pos.promotionZone<2>(to))){
						*m++ = Move{ 0, to, p, 0, 0 };
					}
				}
			}
		}
	}
	return (int)(m - moves);
}


//SFEN�̋ǖʂ�pos��ss�ɃZ�b�g����
void setPosition(Position &pos, Stack *ss, std::istringstream &iss)
{
	//startpos�̏����߂�ǂ�
	//�ύX�����B
	std::string input = iss.str().substr((size_t)iss.tellg() + 1);
	if (input.find("startpos") == 0) {
		input.replace(0, 8, "sfen lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1");
	}
	iss.str(input);

	std::string sfenPos, sfenTurn, sfenHand, sfenCount, sfenMove;

	//�ǖʏ�����
	pos.clear();

	//�Ֆ�
	iss >> sfenPos;//"sfen"
	iss >> sfenPos;
	int x = 0, y = 0, pro = 0;
	for (auto c : sfenPos){
		if ('0' < c && c <= '9'){
			x += c - '0';
		} else if (c == '+'){
			pro = 1;
		} else if (c == '/'){
			x = 0;
			y++;
		} else {
			auto i = sfenPiece.find(c);
			_assert(i != std::string::npos && i < PieceTypeNum);
			int turn = (int)i / HandTypeNum;
			int p = i % HandTypeNum | pro << 3;
			int sq = Position::square(x, y);
			pos.piece_turn[sq] = p | Position::turnMask(turn);
			pro = 0;
			x++;
			if (p == King) pos.king[turn] = sq;//�ʂ̈ʒu�͂����ŃZ�b�g����
		}
	}

	//���
	iss >> sfenTurn;
	pos.turn = (sfenTurn == "b") ? Black : White;

	//������
	iss >> sfenHand;
	int n = 0;
	for (auto c : sfenHand){
		if (c == '-'){
			break;
		} else if ('0' <= c && c <= '9'){
			n = n * 10 + (c - '0');
		} else {
			auto i = sfenPiece.find(c);
			_assert(i != std::string::npos && i < PieceTypeNum);
			pos.hand[i / HandTypeNum][i % HandTypeNum] = (n == 0) ? 1 : n;
			n = 0;
		}
	}

	//�萔(�g��Ȃ�)
	iss >> sfenCount;

	//Stack�͂�����
	ss->checked = pos.inCheck(pos.turn);
	ss->pos = pos;
	pos.ss = ss;

	//�w����
	iss >> sfenMove;
	if (sfenMove != "moves") return;

	while (iss >> sfenMove){
		//�S�Ă̍��@��𐶐����Ĉ�v������̂�T��
		Move moves[MaxMove];
		int n = generateMoves(moves, pos);

		auto it = std::find_if(moves, moves + n, [&](Move move){
			return sfenMove == move.toUSI();
		});
		_assert(it < moves + n);
		pos.doMove(pos.ss++, *it);
		pos.ss->pos = pos;
	}
}


/**
 * @fn int legalMoves2(const std::string)
 * ������sfen�`���̕������ǂݍ��ނƁA���@��̐���Ԃ��B
 * ������u�͊܂܂��A�ł����l�߂̎���܂ށB
 * @param cmd sfen�`���̕�����B�f�t�H���g�͕���ǖʁB
 * @return int ���@��̐��B
 * @detail
 * �����̗�F
 *   "position startpos moves 7g7f"
 * �߂�l�̗�F
 *   30
 */
int legalMoves2(const std::string cmd = std::string("position startpos")) {
    Position pos;
    std::vector<Stack> vss{ MaxGamePly + 2 };
    Stack *const ss = &vss[0];

    std::string token;

    std::istringstream iss(cmd);
    iss >> token;

	std::memset(ss, 0, vss.size() * sizeof *ss);
	setPosition(pos, ss + 1, iss);

	Move moves[MaxMove];  // ���@����i�[���邽�߂̔z��
	int n = generateMoves(moves, pos);  //���ׂĂ̍��@��(������u���܂�)�̌�
	ss->pos = pos;  //���݂̋ǖʂ̕ۑ��B

	int trn_num = 0;  // �߂�l�ƂȂ鍇�@��̐�
	for (int i = 0; i < n; i++){
		Move move = moves[i];
		if (!pos.isLegal(ss, move)) continue;  //������u������
		
		// ���@��̐����J�E���g����B
		trn_num++;
	}
	
	return trn_num;
}


/**
 * @fn char* legal_moves(const char*)
 * ������sfen�`���̕������ǂݍ��ނƁA���@���USI�`���ŕԂ��B
 * ������u���A�ł����l�߂̎���܂܂Ȃ��B
 * @param cmd sfen�`���̕�����B�f�t�H���g�͕���ǖʁB
 * @return char* �����̋ǖʂ̍��@���USI�`���ŗ񋓂���B
 * @detail �߂�l�͌�����Ȃ���΁u""�v��Ԃ��B
 * �����̗�F
 *   "position startpos moves 7g7f"
 * �߂�l�̗�F
 *   "9a9b 7a6b 7a7b�i�ȉ����j"
 */
extern "C" char* legal_moves(const char* cmd = "position startpos") {
    const std::string cmd_str(cmd);
    Position pos;
    std::vector<Stack> vss{ MaxGamePly + 2 };
    Stack *const ss = &vss[0];

    std::string token, rtn_str;
	rtn_str.reserve(MaxMove * 6);  // ���@��1��5����+�X�y�[�X1�����B
	std::string checkSfen;  //�ł����l�߂̃`�F�b�N�p��sfen������B

    std::istringstream iss(cmd_str);
    iss >> token;

	std::memset(ss, 0, vss.size() * sizeof *ss);
	setPosition(pos, ss + 1, iss);

	Move moves[MaxMove];  // ���@����i�[���邽�߂̔z��
	int n = generateMoves(moves, pos);  //���ׂĂ̍��@��(������u���܂�)�̌�
	ss->pos = pos;  //���݂̋ǖʂ̕ۑ��B

	for (int i = 0; i < n; i++){
		Move move = moves[i];
		if (!pos.isLegal(ss, move)) continue;  //������u������

		//���i�߂�
		pos.doMove(ss, move);
		// ����ł�ŁA����ɉ��肪�����邩�B
		if (move.from() == 0 && move.piece() == Pawn && pos.inCheck(pos.turn)){
			//�ł����l�߂̃`�F�b�N�B
			checkSfen = cmd_str + " " + move.toUSI();
			//����Ԃł̂��ׂĂ̍��@��(������u���܂܂Ȃ�)�̌��肪�Ȃ�=�ł����l�߁B
			if (legalMoves2(checkSfen) == 0) {
				//���߂�
				pos = ss->pos;
				continue;  //�ł����l�߂�����
			} 
		}
		//���߂�
		pos = ss->pos;
		
		// ����Ƃ��ċL�^����B
		rtn_str += moves[i].toUSI() + " ";
	}
	// C����ň�����悤�ɂ��Ă����B
	return strdup(rtn_str.c_str());
}


int main() {
	/*
	std::string moves_str = legal_moves("position startpos moves 7g7f 3c3d 2g2f 4c4d 2f2e 2b3c 3i4h 8b4b 5i6h 5a6b 6h7h 3a3b 5g5f 7a7b 4i5h 3b4c 8h7g 6b7a 6g6f 4d4e 7h8h 4c5d 4h5g 6c6d 6i7h 7a8b 5h6g 4a5b 9i9h 9c9d 8h9i 9d9e 7i8h 1c1d 7h7i 1d1e 3g3f 4b2b 2i3g 2b4b 6g6h 4b4a 2h2f 3c4d 2f2g 4d3c 2e2d 2c2d 6f6e 3d3e 7g3c+ 2a3c B*2b 3e3f 2b3c+ 5d6e N*4d 5b4b 3c2b 9e9f 9g9f P*9g 9h9g B*4i 2g2d 3f3g+ P*6b 6a6b 4d3b+ 4b3b 2b3b 4a6a 3b4c 4i7f+ 1i1h P*9h 9i9h N*8f 9h9i");
    std::cout << "moves_str" << std::endl;
    std::cout << moves_str << std::endl;
	*/

    return 0;
}
